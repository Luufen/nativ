"""tee_run (vitarecomp run's session.log tee) with a terminal that blocks every write -- a Windows console with a
selection held in it (QuickEdit) does that. The log and the game must not wait for the terminal, and tee_run must
return once the game is gone. Synthetic: the 'game' is a python child writing ~200 KB to stderr.
Run: python pipeline/tests/test_tee_run.py  (exit 0 = pass)"""
import os, sys, threading, tempfile, time, signal
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import vitarecomp

class Stuck:   # sys.stderr whose writes never return (a console in "Select" mode)
    class _B:
        def write(self, b): threading.Event().wait()
        def flush(self): pass
    buffer = _B()
    def write(self, s): threading.Event().wait()
    def flush(self): pass

d = tempfile.mkdtemp()
log, pidf = os.path.join(d, "session.log"), os.path.join(d, "pid")
child = ("import os,sys; f=open(%r,'w'); f.write(str(os.getpid())); f.close()\n"
         "for i in range(4000): sys.stderr.write('line %%05d %%s\\n' %% (i, 'x'*40))\n"
         "sys.stderr.flush()\n") % pidf
res = {}
real = sys.stderr
sys.stderr = Stuck()
th = threading.Thread(target=lambda: res.setdefault("rc", vitarecomp.tee_run([sys.executable, "-c", child], None, d, log)), daemon=True)
th.start()
th.join(8)
killed = False
if th.is_alive() and os.path.exists(pidf):   # the child blocked in its own stderr write: end it the way a closed window would
    try: os.kill(int(open(pidf).read()), signal.SIGTERM); killed = True
    except OSError: pass
th.join(8)
sys.stderr = real
lines = open(log, "rb").read().count(b"\n") if os.path.exists(log) else 0
fails = []
if th.is_alive(): fails.append("tee_run did not return after the game ended (runner hangs)")
if killed: fails.append("the game blocked on its stderr (had to be killed)")
if lines != 4000: fails.append("session.log holds %d of 4000 lines" % lines)
if res.get("rc") not in (0, None) or ("rc" not in res and not th.is_alive()): fails.append("rc %r" % res.get("rc"))
print("tee_run: returned=%s game_killed=%s log_lines=%d rc=%r" % (not th.is_alive(), killed, lines, res.get("rc")))
print("FAIL: " + "; ".join(fails) if fails else "ok")
sys.stdout.flush(); os._exit(1 if fails else 0)
