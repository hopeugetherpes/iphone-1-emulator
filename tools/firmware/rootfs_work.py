"""Derive a per-run "work image" from the user's extracted 1A543a root
filesystem. The genuine image is never modified; the copy carries the few
emulator-side configuration edits the single-ramdisk boot needs.

Existing files can be replaced in place. New files and directories are added
with leaf-only catalog insertion and a single contiguous data extent; catalog
node splitting and catalog extents overflow are intentionally unsupported.

Usage:
  rootfs_work.py <rootfs.img> <work.img>
      [--fstab "LINE"]                 rewrite /etc/fstab to one line
      [--replace NAME=HOSTFILE]...     replace file NAME (catalog leaf name)
      [--launchd-env NAME=K=V,...]     add EnvironmentVariables to a launchd
                                       plist NAME (binary plist rewritten)
      [--no-autolock]                 set SpringBoard auto-lock and dim to Never
      [--add GUEST_PATH=HOSTFILE]...  add a new file, creating directories
"""
import os
import plistlib
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hfsx_extract as H  # noqa: E402


class Work:
    def __init__(self, path):
        self.vol = H.Volume(path)
        self.path = path
        self.bs = self.vol.block_size
        self.cat = bytearray(self.vol.read_fork(self.vol.catalog))
        self.node_size = H.u16(self.cat, 14 + 18)
        self.first_leaf = H.u32(self.cat, 14 + 10)
        self.catalog_leaf_records = H.u32(self.cat, 14 + 6)
        with open(path, "rb") as f:
            f.seek(1024)
            self.vh = bytearray(f.read(512))
        self.alloc = bytearray(self.vol.read_fork(H.Fork(self.vh, 112)))
        self.next_catalog_id = H.u32(self.vh, 64)
        # This image stores the HFSX compare byte at the end of the packed
        # B-tree header fields.  0xbc is kHFSBinaryCompare.
        compare_candidates = (14 + 37, 14 + 35, 14 + 22)
        self.key_compare_type = next(
            (self.cat[o] for o in compare_candidates
             if self.cat[o] in (0xBC, 0xCF)), None)
        if self.key_compare_type is None:
            raise SystemExit("catalog key compare type is unknown")

    def cat_disk_off(self, fork_off):
        """Map an offset inside the catalog fork to a disk offset."""
        rem = fork_off
        for start, count in self.vol.catalog.extents:
            n = count * self.bs
            if rem < n:
                return start * self.bs + rem
            rem -= n
        raise SystemExit("catalog offset out of extents")

    def alloc_disk_off(self, fork_off):
        rem = fork_off
        fork = H.Fork(self.vh, 112)
        for start, count in fork.extents:
            n = count * self.bs
            if rem < n:
                return start * self.bs + rem
            rem -= n
        raise SystemExit("allocation offset out of extents")

    def _fold_name(self, name):
        if self.key_compare_type == 0xCF:
            # All names inserted by this tool are ASCII.
            return tuple(ord(c.lower()) if "A" <= c <= "Z" else ord(c)
                         for c in name)
        return tuple(ord(c) for c in name)

    def _key(self, raw):
        key_len = H.u16(raw, 0)
        if key_len < 6:
            raise SystemExit("malformed catalog key")
        name_len = H.u16(raw, 6)
        name = raw[8:8 + name_len * 2].decode("utf-16-be")
        return H.u32(raw, 2), self._fold_name(name)

    def _leaf_records(self, node):
        nrecs = H.u16(node, 10)
        out = []
        for i in range(nrecs):
            off = H.u16(node, self.node_size - 2 * (i + 1))
            end = H.u16(node, self.node_size - 2 * (i + 2))
            if off < 14 or off >= end or end > self.node_size:
                raise SystemExit("malformed catalog leaf record offsets")
            out.append((off, end, bytes(node[off:end])))
        return out

    def _leaf_chain(self):
        node_index = self.first_leaf
        seen = set()
        while node_index:
            if node_index in seen:
                raise SystemExit("catalog leaf chain loops at node %d" % node_index)
            seen.add(node_index)
            base = node_index * self.node_size
            node = self.cat[base:base + self.node_size]
            if len(node) != self.node_size or node[8] != 0xFF:
                raise SystemExit("catalog node %d is not a leaf" % node_index)
            yield node_index, node
            node_index = H.u32(node, 0)

    def _write_vh_u32(self, off, value):
        struct.pack_into(">I", self.vh, off, value)
        with open(self.path, "r+b") as f:
            for base in (1024, self.vol.total_blocks * self.bs - 1024):
                f.seek(base + off)
                f.write(struct.pack(">I", value))

    def _write_catalog_node(self, node_index, records):
        base = node_index * self.node_size
        old = self.cat[base:base + self.node_size]
        used = 14 + sum(len(r) for r in records) + 2 * (len(records) + 1)
        if used > self.node_size:
            raise SystemExit("catalog leaf %d full, node split not implemented"
                             % node_index)
        node = bytearray(self.node_size)
        node[:14] = old[:14]
        struct.pack_into(">H", node, 10, len(records))
        pos = 14
        offsets = []
        for record in records:
            offsets.append(pos)
            node[pos:pos + len(record)] = record
            pos += len(record)
        offsets.append(pos)
        for i, off in enumerate(offsets):
            struct.pack_into(">H", node, self.node_size - 2 * (i + 1), off)
        self.cat[base:base + self.node_size] = node
        with open(self.path, "r+b") as f:
            f.seek(self.cat_disk_off(base))
            f.write(node)

    def _write_catalog_leaf_records(self):
        struct.pack_into(">I", self.cat, 14 + 6, self.catalog_leaf_records)
        with open(self.path, "r+b") as f:
            f.seek(self.cat_disk_off(14 + 6))
            f.write(struct.pack(">I", self.catalog_leaf_records))

    def _insert_catalog_record(self, record):
        key = self._key(record)
        selected = None
        for node_index, node in self._leaf_chain():
            records = self._leaf_records(node)
            if not records or self._key(records[-1][2]) >= key:
                selected = (node_index, records)
                break
            selected = (node_index, records)
        if selected is None:
            raise SystemExit("catalog has no leaf nodes")
        node_index, records = selected
        ordered = [r[2] for r in records]
        pos = 0
        while pos < len(ordered) and self._key(ordered[pos]) < key:
            pos += 1
        if pos < len(ordered) and self._key(ordered[pos]) == key:
            raise SystemExit("catalog key already exists")
        ordered.insert(pos, record)
        self._write_catalog_node(node_index, ordered)
        self.catalog_leaf_records += 1
        self._write_catalog_leaf_records()

    def _find_record(self, parent, name, rtype=None):
        for _, node in self._leaf_chain():
            for _, _, raw in self._leaf_records(node):
                p = H.parse_record(raw)
                if p and p[0] == parent and p[1] == name and (
                        rtype is None or p[2] == rtype):
                    return p
        return None

    def _find_object(self, parent, name):
        p = self._find_record(parent, name)
        if not p or p[2] not in (1, 2):
            return None
        return p[2], H.u32(p[4], p[3] + 8), p

    def _resolve_path(self, path, current=2, seen=None):
        seen = set() if seen is None else seen
        for component in [x for x in path.split("/") if x]:
            obj = self._find_object(current, component)
            if obj is None:
                return None
            if obj[0] == 1:
                current = obj[1]
                continue
            marker = (current, component)
            if marker in seen:
                return None
            seen.add(marker)
            fork = H.Fork(obj[2][4], obj[2][3] + 88)
            target = self.vol.read_fork(fork).decode("utf-8").rstrip("\0")
            if not target:
                return None
            base = 2 if target.startswith("/") else current
            current = self._resolve_path(target, base, seen)
            if current is None:
                return None
        return current

    def _resolve_dir(self, path):
        return self._resolve_path(path)

    def _bump_valence(self, directory_id):
        found = None
        for node_index, node in self._leaf_chain():
            for off, _, raw in self._leaf_records(node):
                p = H.parse_record(raw)
                if p and p[2] == 1 and H.u32(raw, p[3] + 8) == directory_id:
                    found = (node_index, off, p)
                    break
            if found:
                break
        if not found:
            raise SystemExit("cannot find folder record for CNID %d" % directory_id)
        node_index, off, p = found
        value_off = node_index * self.node_size + off + p[3] + 4
        value = H.u32(self.cat, value_off) + 1
        self.cat[value_off:value_off + 4] = struct.pack(">I", value)
        with open(self.path, "r+b") as f:
            f.seek(self.cat_disk_off(value_off))
            f.write(struct.pack(">I", value))

    def _catalog_object_ids(self):
        ids = set()
        for _, node in self._leaf_chain():
            for _, _, raw in self._leaf_records(node):
                p = H.parse_record(raw)
                if p and p[2] in (1, 2):
                    ids.add(H.u32(raw, p[3] + 8))
        return ids

    def _new_catalog_id(self):
        # Reuse an unused CNID hole so added records can stay in an existing
        # leaf under the no-node-split policy.  HFS+ permits any unused
        # positive CNID; nextCatalogID remains monotonic for new files.
        used = self._catalog_object_ids()
        for ident in range(16, self.next_catalog_id):
            if ident not in used:
                return ident
        ident = self.next_catalog_id
        self.next_catalog_id += 1
        return ident

    def _allocate_blocks(self, count):
        if not count:
            return 0
        run = 0
        start = 0
        for block in range(self.vol.total_blocks):
            used = self.alloc[block >> 3] & (0x80 >> (block & 7))
            if used:
                run = 0
            else:
                if run == 0:
                    start = block
                run += 1
                if run == count:
                    for n in range(start, start + count):
                        self.alloc[n >> 3] |= 0x80 >> (n & 7)
                    with open(self.path, "r+b") as f:
                        f.seek(self.alloc_disk_off(start >> 3))
                        f.write(self.alloc[start >> 3:])
                    self._write_vh_u32(48, H.u32(self.vh, 48) - count)
                    return start
        raise SystemExit("no contiguous allocation run of %d blocks" % count)

    @staticmethod
    def _make_key(parent, name):
        encoded = name.encode("utf-16-be")
        key_len = 6 + len(encoded)
        return struct.pack(">HIH", key_len, parent, len(name)) + encoded

    def _new_folder(self, parent, name):
        sibling = self._find_record(1451, "Preferences", 1)
        if not sibling:
            raise SystemExit("folder template /var/root/Library/Preferences missing")
        data = bytearray(sibling[4][sibling[3]:])
        ident = self._new_catalog_id()
        struct.pack_into(">I", data, 4, 0)
        struct.pack_into(">I", data, 8, ident)
        self._insert_catalog_record(self._make_key(parent, name) + data)
        thread = (self._make_key(ident, "") +
                  struct.pack(">HHIH", 3, 0, parent, len(name)) +
                  name.encode("utf-16-be"))
        self._insert_catalog_record(thread)
        self._bump_valence(parent)
        self._write_vh_u32(36, H.u32(self.vh, 36) + 1)
        self._write_vh_u32(64, self.next_catalog_id)
        return ident

    def _new_file(self, parent, name, data_bytes):
        sibling = self._find_record(1452, ".GlobalPreferences.plist", 2)
        if not sibling:
            raise SystemExit("file template /var/root/Library/Preferences missing")
        data = bytearray(sibling[4][sibling[3]:])
        ident = self.next_catalog_id
        self.next_catalog_id += 1
        blocks = (len(data_bytes) + self.bs - 1) // self.bs
        start = self._allocate_blocks(blocks)
        struct.pack_into(">H", data, 2, 0x0002)
        struct.pack_into(">I", data, 8, ident)
        data[44:52] = b"\0" * 8
        fork = 88
        struct.pack_into(">Q", data, fork, len(data_bytes))
        struct.pack_into(">I", data, fork + 8, 0)
        struct.pack_into(">I", data, fork + 12, blocks)
        data[fork + 16:fork + 80] = b"\0" * 64
        if blocks:
            struct.pack_into(">II", data, fork + 16, start, blocks)
            with open(self.path, "r+b") as f:
                f.seek(start * self.bs)
                f.write(data_bytes.ljust(blocks * self.bs, b"\0"))
        self._insert_catalog_record(self._make_key(parent, name) + data)
        thread = (self._make_key(ident, "") +
                  struct.pack(">HHIH", 4, 0, parent, len(name)) +
                  name.encode("utf-16-be"))
        self._insert_catalog_record(thread)
        self._bump_valence(parent)
        self._write_vh_u32(32, H.u32(self.vh, 32) + 1)
        self._write_vh_u32(64, self.next_catalog_id)

    def add_file(self, guest_path, host_path):
        with open(host_path, "rb") as f:
            data = f.read()
        self.add_file_data(guest_path, data)

    def add_file_data(self, guest_path, data):
        components = [x for x in guest_path.split("/") if x]
        if not guest_path.startswith("/") or not components:
            raise SystemExit("--add guest path must be absolute: %s" % guest_path)
        parent = 2
        for component in components[:-1]:
            obj = self._find_object(parent, component)
            obj_id = self._resolve_path(component, parent)
            if obj is None:
                obj_id = self._new_folder(parent, component)
            elif obj_id is None:
                raise SystemExit("%s is not a directory" % component)
            parent = obj_id
        name = components[-1]
        if self._find_record(parent, name):
            raise SystemExit("%s already exists" % guest_path)
        self._new_file(parent, name, data)
        print("%s: added %d bytes" % (guest_path, len(data)))

    def find_file_path(self, guest_path):
        components = [x for x in guest_path.split("/") if x]
        if not guest_path.startswith("/") or not components:
            raise SystemExit("file path must be absolute: %s" % guest_path)
        parent = 2
        for component in components[:-1]:
            parent = self._resolve_path(component, parent)
            if parent is None:
                return None
        for node_index, node in self._leaf_chain():
            for off, _, raw in self._leaf_records(node):
                record = H.parse_record(raw)
                if (record and record[0] == parent and
                        record[1] == components[-1] and record[2] == 2):
                    fork = H.Fork(raw, record[3] + 88)
                    fork_off = (node_index * self.node_size + off +
                                record[3] + 88)
                    return (fork, fork_off, parent, components[-1])
        return None

    def find_file(self, needle):
        hits = []
        node = self.first_leaf
        while node:
            base = node * self.node_size
            n = self.cat[base:base + self.node_size]
            nrecs = H.u16(n, 10)
            for i in range(nrecs):
                off = H.u16(n, self.node_size - 2 * (i + 1))
                end = H.u16(n, self.node_size - 2 * (i + 2))
                if off >= end or end > self.node_size:
                    continue
                p = H.parse_record(n[off:end])
                if not p:
                    continue
                parent, name, rtype, doff, raw = p
                if rtype == 2 and name == needle:
                    hits.append((base + off + doff + 88, H.Fork(raw, doff + 88)))
            node = H.u32(n, 0)
        if len(hits) != 1:
            raise SystemExit("%r: %d catalog hits (need exactly 1)" % (needle, len(hits)))
        return hits[0]

    def read_file_path(self, guest_path):
        hit = self.find_file_path(guest_path)
        if not hit:
            raise SystemExit("%s: file not found" % guest_path)
        fork, _, _, _ = hit
        return self.vol.read_fork(fork)

    def read_file(self, name):
        _, fork = self.find_file(name)
        return self.vol.read_fork(fork)

    def replace_file(self, name, data):
        fork_off, fork = self.find_file(name)
        self._replace_fork(name, fork_off, fork, data)

    def replace_file_path(self, guest_path, data):
        hit = self.find_file_path(guest_path)
        if not hit:
            raise SystemExit("%s: file not found" % guest_path)
        fork, fork_off, _, _ = hit
        self._replace_fork(guest_path, fork_off, fork, data)

    def _replace_fork(self, name, fork_off, fork, data):
        cap = fork.total_blocks * self.bs
        if len(data) > cap:
            raise SystemExit("%s: %d bytes exceed the %d bytes the file's blocks hold"
                             % (name, len(data), cap))
        with open(self.path, "r+b") as f:
            rem = data
            for start, count in fork.extents:
                chunk = rem[:count * self.bs]
                rem = rem[len(chunk):]
                f.seek(start * self.bs)
                f.write(chunk.ljust(count * self.bs, b"\0"))
                if not rem:
                    break
            f.seek(self.cat_disk_off(fork_off))
            f.write(struct.pack(">Q", len(data)))
        print("%s: %d -> %d bytes (blocks %s)" % (name, fork.logical, len(data), fork.extents))
        # keep the in-memory catalog coherent for later lookups
        c = bytearray(self.cat)
        c[fork_off:fork_off + 8] = struct.pack(">Q", len(data))
        self.cat = c


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        raise SystemExit(__doc__)
    src, dst = a[0], a[1]
    shutil.copyfile(src, dst)
    w = Work(dst)
    i = 2
    no_autolock = False
    while i < len(a):
        if a[i] == "--fstab":
            line = a[i + 1]
            w.replace_file("fstab", (line.rstrip("\n") + "\n").encode())
            i += 2
        elif a[i] == "--replace":
            name, host = a[i + 1].split("=", 1)
            w.replace_file(name, open(host, "rb").read())
            i += 2
        elif a[i] == "--launchd-env":
            name, spec = a[i + 1].split("=", 1)
            pl = plistlib.loads(w.read_file(name))
            env = pl.setdefault("EnvironmentVariables", {})
            for kv in spec.split(","):
                k, v = kv.split("=", 1)
                env[k] = v
            w.replace_file(name, plistlib.dumps(pl, fmt=plistlib.FMT_BINARY))
            i += 2
        elif a[i] == "--no-autolock":
            no_autolock = True
            i += 1
        elif a[i] == "--add":
            guest, host = a[i + 1].split("=", 1)
            w.add_file(guest, host)
            i += 2
        else:
            raise SystemExit("unknown option %r" % a[i])
    if no_autolock:
        path = "/var/root/Library/Preferences/com.apple.springboard.plist"
        existing = w.find_file_path(path)
        if existing:
            fork, _, _, _ = existing
            pl = plistlib.loads(w.vol.read_fork(fork))
        else:
            pl = {}
        pl["SBAutoLockTime"] = -1
        pl["SBAutoDimTime"] = -1
        data = plistlib.dumps(pl, fmt=plistlib.FMT_XML)
        if existing:
            w.replace_file_path(path, data)
        else:
            w.add_file_data(path, data)


if __name__ == "__main__":
    main()
