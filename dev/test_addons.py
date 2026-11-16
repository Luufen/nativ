#!/usr/bin/env python3
"""Add-ons and updates in the library, end to end through vitarecomp itself (DLC-01, UPD-UI; no game: synthetic add-on
PKGs built with the decrypter's own test helpers, made-up licences):

  add <an add-on PKG> without a licence: refused, nothing kept; with its licence: decrypted into
  dump/decrypted/<title>/addcont/<name>, the licence kept (added again it needs no licence file), its game's
  recomp/fs/addcont0 linked there; a licence-only add-on (no files, no PFS layer) is added too; addons lists both;
  the folder scan says "added" for them and "addon" for one not added; remove-addon takes one out (the link stays
  while another is there, goes with the last); remove-update without an update is refused.
  Fail-before (the tree before DLC-01): `add` of an add-on said "nothing to add" and kept nothing.

  python3 dev/test_addons.py        (pycryptodome needed, as for the decrypter)
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "decrypt"))
from vitadecrypt import pfs as PFS, pkg as PKG, rif as RIF, sfo as SFO  # noqa: E402
from vitadecrypt.game import _pfs_encrypt_member, _pfs_verify_only_member  # noqa: E402

TID = "PCSZ00043"
KLIC = bytes.fromhex("102132435465768798a9bacbdcedfe0f")


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def addon_pkg(path, name, title, data=None):
    """a synthetic add-on PKG of TID: with a data file (PFS-protected) or only its licence"""
    cid = f"IP0000-{TID}_00-{name}"
    sfo = SFO.build_sfo({"APP_VER": "01.00", "CATEGORY": "ac", "TITLE_ID": TID, "CONTENT_ID": cid, "TITLE": title})
    files = {"sce_sys/param.sfo": sfo}
    if data is not None:
        sigs, box = _pfs_encrypt_member(data, KLIC)
        files_db = PFS._build_files_db(KLIC, 0x1000, version=4, image_spec=1, entries=[
            (0, "sce_sys", 1, PFS.FT_SYS_DIR, 0), (1, "param.sfo", 2, PFS.FT_UNENC_SYS_RO, len(sfo)),
            (0, "level.dat", 3, PFS.FT_NORMAL_FILE, len(data))])
        unicv = PFS._build_unicv_db([
            {"num_sectors": len(PFS._chunks(sfo, PFS.FILE_SECTOR_SIZE)), "version": 1, "dbseed": None, "make_sigs": _pfs_verify_only_member(sfo, KLIC)},
            {"num_sectors": len(PFS._chunks(data, PFS.FILE_SECTOR_SIZE)), "version": 1, "dbseed": None, "make_sigs": sigs}])
        files.update({"sce_pfs/files.db": files_db, "sce_pfs/unicv.db": unicv, "level.dat": box["bytes"]})
    put(path, PKG._build_synthetic_pkg(sfo, files, key_type=2, content_type=PKG.CONTENT_TYPE_DLC, content_id=cid))
    put(path[:-4] + ".rif", RIF.build_rif(cid, KLIC))
    return cid


checks = fails = 0


def check(what, ok):
    global checks, fails
    checks += 1
    print(f"{what:<78} {'ok' if ok else 'FAILED'}")
    if not ok:
        fails += 1


def main():
    with tempfile.TemporaryDirectory() as tmp:
        lib = os.path.join(tmp, "lib")
        env = dict(os.environ, ATIV_DUMP=lib, ATIV_PREFS=os.path.join(tmp, "prefs"), PYTHONUTF8="1")
        vr = [sys.executable, os.path.join(ROOT, "pipeline", "vitarecomp.py")]

        def run(*args):
            return subprocess.run(vr + list(args), env=env, capture_output=True, text=True)

        W = os.path.join(lib, "decrypted", TID)
        level = b"a level of the add-on " * 300
        dl = os.path.join(tmp, "dl")
        addon_pkg(os.path.join(dl, "maps.pkg"), "MAPPACK000000001", "Map Pack", level)
        addon_pkg(os.path.join(dl, "costume.pkg"), "COSTUME000000002", "Costume")
        addon_pkg(os.path.join(dl, "later.pkg"), "LATERPACK0000003", "Later Pack", b"x" * 100)
        os.makedirs(os.path.join(W, "recomp", "fs"))   # (the game's build: its run's file system)

        r = run("add", os.path.join(dl, "maps.pkg"))
        check("an add-on without its licence is refused", r.returncode != 0 and "needs its own licence" in r.stdout + r.stderr)
        check("  and nothing is kept", not os.path.exists(os.path.join(W, "addcont")))

        r = run("add", os.path.join(dl, "maps.pkg"), "--license", os.path.join(dl, "maps.rif"))
        check("an add-on with its licence is added", r.returncode == 0)
        d = os.path.join(W, "addcont", "MAPPACK000000001")
        check("  its files decrypted into addcont/<name>", os.path.isfile(os.path.join(d, "level.dat")) and open(os.path.join(d, "level.dat"), "rb").read() == level)
        check("  its licence kept (licenses/<content id>.rif)", os.path.isfile(os.path.join(lib, "licenses", f"IP0000-{TID}_00-MAPPACK000000001.rif")))
        a0 = os.path.join(W, "recomp", "fs", "addcont0")
        check("  the game's addcont0 links to the add-ons", os.path.isfile(os.path.join(a0, "MAPPACK000000001", "level.dat")))

        r = run("add", os.path.join(dl, "maps.pkg"))
        check("added again it finds its kept licence", r.returncode == 0 and "kept" in r.stdout)

        r = run("add", os.path.join(dl, "costume.pkg"), "--license", os.path.join(dl, "costume.rif"))
        check("a licence-only add-on (no files) is added", r.returncode == 0 and os.path.isdir(os.path.join(W, "addcont", "COSTUME000000002", "sce_sys")))
        check("  no licence file among its files", not os.path.exists(os.path.join(W, "addcont", "COSTUME000000002", "sce_sys", "package", "work.bin")))

        r = run("addons", TID)
        rows = [ln.split("\t") for ln in r.stdout.splitlines() if ln.strip()]
        check("addons lists both, by name, with title and content id",
              [(x[0], x[1], x[3]) for x in rows] == [("COSTUME000000002", "Costume", f"IP0000-{TID}_00-COSTUME000000002"),
                                                      ("MAPPACK000000001", "Map Pack", f"IP0000-{TID}_00-MAPPACK000000001")])

        r = run("scan", dl)
        st = {ln.split("\t")[7].rsplit(os.sep, 1)[-1]: ln.split("\t")[4] for ln in r.stdout.splitlines() if ln.startswith("addon")}
        check("the scan: added ones 'added', the other 'addon'", st == {"maps.pkg": "added", "costume.pkg": "added", "later.pkg": "addon"})

        r = run("remove-addon", TID, "MAPPACK000000001")
        check("remove-addon takes it out", r.returncode == 0 and not os.path.exists(d))
        check("  the link stays for the other one", os.path.isdir(os.path.join(a0, "COSTUME000000002")))
        r = run("remove-addon", TID, "../x")
        check("remove-addon refuses a name that isn't one", r.returncode != 0)
        run("remove-addon", TID, "COSTUME000000002")
        check("with the last one gone, the game has no addcont0", not os.path.lexists(a0))

        r = run("remove-update", TID)
        check("remove-update without an update is refused", r.returncode != 0 and "no update" in r.stdout + r.stderr)

    print(f"{checks} checks, all passed" if not fails else f"{fails} of {checks} checks FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
