# Screenshot over serial WITHOUT resetting the board: raw fd, no DTR/RTS changes.
# Run `stty -F /dev/ttyACM0 115200 raw -echo -hupcl` once, then: PORT=/dev/ttyACM0 python3 screenshot.py out.png [serial cmds sent first, e.g. 2 J]
import os, sys, time, select, struct, zlib
fd = os.open(os.environ.get('PORT', '/dev/ttyACM0'), os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
def rd(t):
    out = b''; t0 = time.time()
    while time.time() - t0 < t:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: out += os.read(fd, 65536)
            except BlockingIOError: pass
    return out
for c in sys.argv[2:]:
    os.write(fd, c.encode('utf-8')); rd(1.2)
rd(0.2)
os.write(fd, b'S')
buf = b''; t0 = time.time()
while time.time() - t0 < 15:
    buf += rd(0.3)
    i = buf.find(b'SNAP ')
    if i >= 0:
        j = buf.find(b'\n', i)
        if j > 0:
            _, w, h, n = buf[i:j].split(); w, h, n = int(w), int(h), int(n)
            data = buf[j+1:]
            while len(data) < n and time.time() - t0 < 30: data += rd(0.2)
            break
else:
    print('no header'); sys.exit(1)
data = data[:n]
rows = []
for y in range(h):
    row = bytearray([0])
    for x in range(w):
        v = data[(y*w+x)*2] | (data[(y*w+x)*2+1] << 8)
        row += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    rows.append(bytes(row))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
open(sys.argv[1], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b''))
print('saved', sys.argv[1])
