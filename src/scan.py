import asyncio
from bleak import BleakScanner

async def run():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover()
    for d in devices:
        print(f"Device: {d.name} | Address: {d.address} | RSSI: {d.rssi}")

loop = asyncio.get_event_loop()
loop.run_until_complete(run())
