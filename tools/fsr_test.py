#!/usr/bin/env python3
"""
Foot sensor board test - runs on the Pi (or a PC) with the U2D2.

    pip install dynamixel-sdk

Examples:
    python3 fsr_test.py                     # ping ID 30, then print FSRs
    python3 fsr_test.py --id 31             # talk to the right-foot board
    python3 fsr_test.py --set-id 31         # change board ID 30 -> 31 (saved)
    python3 fsr_test.py --scan              # ping IDs 0-252 one by one

Register map (little-endian), read in one go at address 128, length 12:
    128..135  FSR0..FSR3 raw 12-bit ADC (uint16 each)
    136..139  sample counter (uint32, +1 per 1 kHz ADC sweep)
"""
import argparse
import time
from dynamixel_sdk import PortHandler, PacketHandler, COMM_SUCCESS

ADDR_ID = 7
ADDR_HW_STATUS = 70
ADDR_FSR0 = 128
LEN_FSR_BLOCK = 12


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=4500000)
    ap.add_argument("--id", type=int, default=30)
    ap.add_argument("--set-id", type=int, help="write a new ID to the board")
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--hz", type=float, default=100.0, help="poll rate")
    args = ap.parse_args()

    port = PortHandler(args.port)
    ph = PacketHandler(2.0)
    if not port.openPort() or not port.setBaudRate(args.baud):
        raise SystemExit(f"cannot open {args.port} at {args.baud}")

    if args.scan:
        for i in range(0, 253):
            model, res, err = ph.ping(port, i)
            if res == COMM_SUCCESS:
                print(f"ID {i:3d}: model {model}")
        return

    model, res, err = ph.ping(port, args.id)
    if res != COMM_SUCCESS:
        raise SystemExit(f"no reply from ID {args.id}: {ph.getTxRxResult(res)}")
    print(f"ID {args.id}: model {model} (foot board = 9000)")

    if args.set_id is not None:
        res, err = ph.write1ByteTxRx(port, args.id, ADDR_ID, args.set_id)
        print("set ID:", ph.getTxRxResult(res), ph.getRxPacketError(err) if err else "")
        return

    status, res, err = ph.read1ByteTxRx(port, args.id, ADDR_HW_STATUS)
    if res == COMM_SUCCESS:
        print(f"hardware status flags: 0x{status:02X}")

    period = 1.0 / args.hz
    ok = fail = 0
    t_report = time.time()
    while True:
        t0 = time.time()
        data, res, err = ph.readTxRx(port, args.id, ADDR_FSR0, LEN_FSR_BLOCK)
        if res == COMM_SUCCESS and err == 0:
            ok += 1
            fsr = [data[i] | (data[i + 1] << 8) for i in range(0, 8, 2)]
            count = int.from_bytes(bytes(data[8:12]), "little")
            print(f"\rFSR {fsr[0]:4d} {fsr[1]:4d} {fsr[2]:4d} {fsr[3]:4d}  n={count:<10d}", end="")
        else:
            fail += 1
        if time.time() - t_report >= 2.0:
            print(f"\n[{ok / (time.time() - t_report):.0f} reads/s, {fail} failed]")
            ok = fail = 0
            t_report = time.time()
        time.sleep(max(0.0, period - (time.time() - t0)))


if __name__ == "__main__":
    main()
