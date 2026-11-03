import sys, hashlib
sys.path.insert(0, sys.argv[1])
from elekloader import syx
s = syx.Syx.load(sys.argv[2])
print('device', hex(s.device_id), 'version', s.version, 'sections', [(t[0], hex(t[3])) for t in s.table])
img = s.section(3)
print('main', len(img), hashlib.sha256(img).hexdigest())
open(sys.argv[3], 'wb').write(img)
