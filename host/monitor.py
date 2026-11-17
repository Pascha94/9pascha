"""Read NIMRODCORE MAKCU v0.7 packets from USB2 CH343, no Windows Raw Input.
Requires: pip install pyserial. Diagnostic only, not a controller remapper.
"""
import argparse, struct, time
import serial

def crc16(data):
    crc=0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--port',default='COM3')
    args=ap.parse_args()
    with serial.Serial(args.port,4000000,timeout=.05,write_timeout=1) as port:
        port.reset_input_buffer()
        port.write(b'NC:HELLO\r\n')
        reply=port.readline().decode('ascii','replace').strip()
        print('Handshake:',reply)
        if not reply.startswith('NC:READY protocol=1'):
            raise RuntimeError('Not NIMRODCORE firmware')
        port.write(b'NC:START\r\n')
        print('Stream:',port.readline().decode('ascii','replace').strip())
        buf=bytearray()
        next_ping=time.monotonic()+1
        try:
            while True:
                if time.monotonic()>=next_ping:
                    port.write(b'NC:KEEPALIVE\r\n')
                    next_ping=time.monotonic()+1
                buf.extend(port.read(512))
                while len(buf)>=5:
                    if buf[:2]!=b'NC' or buf[2]!=1 or buf[4]>64:
                        del buf[0];continue
                    total=13+buf[4]
                    if len(buf)<total:break
                    if crc16(buf[2:11+buf[4]]) != int.from_bytes(buf[11+buf[4]:total],'little'):
                        del buf[0];continue
                    kind=buf[3];payload=bytes(buf[11:11+buf[4]])
                    if kind==1 and len(payload)==8:
                        buttons,dx,dy,wheel,pan,tilt=struct.unpack('<Bhhbbb',payload)
                        print('MOUSE',buttons,dx,dy,wheel,pan,tilt)
                    elif kind==2 and len(payload)==15:
                        print('KEYBOARD',payload[0],list(payload[1:]))
                    elif kind==3 and len(payload)==5:
                        print('STATUS',payload[0],'connected; dropped',int.from_bytes(payload[1:],'little'))
                    del buf[:total]
        finally:
            port.write(b'NC:STOP\r\n')

if __name__=='__main__':main()
