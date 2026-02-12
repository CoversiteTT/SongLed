"""
SongLed BLE Bridge - Python test client for Bluetooth LE communication
Requires: bleak library (pip install bleak)
"""

import asyncio
import sys
from bleak import BleakClient, BleakScanner

# Service and Characteristic UUIDs (must match ESP32)
SONGLED_SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
CHAR_CMD_TX_UUID = "12345678-1234-5678-1234-56789abcdef1"  # ESP32 -> PC
CHAR_CMD_RX_UUID = "12345678-1234-5678-1234-56789abcdef2"  # PC -> ESP32
CHAR_COVER_UUID = "12345678-1234-5678-1234-56789abcdef3"   # PC -> ESP32 (cover)
CHAR_STATUS_UUID = "12345678-1234-5678-1234-56789abcdef4"  # ESP32 -> PC (status)


class SongLedBleClient:
    def __init__(self):
        self.client = None
        self.device = None
        self.rx_buffer = ""
        self._keepalive_task = None
        
    async def find_device(self):
        """Scan for SongLed device"""
        print("Scanning for SongLed device...")
        devices = await BleakScanner.discover(timeout=5.0)

        candidates = [d for d in devices if d.name and "SongLed" in d.name]
        if not candidates:
            print("SongLed device not found")
            return False

        if len(candidates) == 1:
            device = candidates[0]
            print(f"Found SongLed device: {device.name} ({device.address})")
            self.device = device
            return True

        print("Multiple SongLed devices found:")
        for idx, dev in enumerate(candidates, 1):
            print(f"  {idx}. {dev.name} ({dev.address})")

        while True:
            choice = input("Select device number: ").strip()
            if not choice.isdigit():
                continue
            idx = int(choice)
            if 1 <= idx <= len(candidates):
                self.device = candidates[idx - 1]
                return True
    
    async def connect(self):
        """Connect to the device"""
        if not self.device:
            if not await self.find_device():
                return False
        
        print(f"Connecting to {self.device.address}...")
        self.client = BleakClient(self.device.address)
        
        try:
            await self.client.connect()
            print("Connected successfully!")
            
            # Enable notifications for CMD_TX (ESP32 -> PC)
            await self.client.start_notify(CHAR_CMD_TX_UUID, self._handle_notification)
            print("Notifications enabled")

            # Send an initial line to unlock ESP32 notifications
            await self.send_line("HELLO OK")

            # Start keepalive
            self._keepalive_task = asyncio.create_task(self._keepalive())
            
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False

    async def _keepalive(self):
        """Periodic keepalive to keep the link active"""
        try:
            while self.client and self.client.is_connected:
                await self.send_line("HELLO OK")
                await asyncio.sleep(5.0)
        except Exception:
            pass
    
    def _handle_notification(self, sender, data):
        """Handle incoming notifications from ESP32"""
        try:
            text = data.decode('utf-8')
            self.rx_buffer += text
            
            # Process complete lines
            while '\n' in self.rx_buffer:
                line, self.rx_buffer = self.rx_buffer.split('\n', 1)
                line = line.strip()
                if line:
                    print(f"<< {line}")
                    self._process_command(line)
        except Exception as e:
            print(f"Error processing notification: {e}")
    
    def _process_command(self, line):
        """Process commands from ESP32"""
        if line.startswith("VOL GET"):
            # ESP32 wants current volume
            asyncio.create_task(self.send_line("VOL 50"))
        elif line.startswith("VOL SET"):
            # ESP32 is setting volume
            vol = line.split()[2]
            print(f"Volume set to {vol}%")
        elif line == "MUTE":
            print("Mute toggled")
        elif line == "SPK LIST":
            # Send speaker list
            asyncio.create_task(self.send_speaker_list())
        elif line == "HELLO":
            asyncio.create_task(self.send_line("HELLO OK"))
    
    async def send_line(self, line):
        """Send a line to ESP32"""
        if not self.client or not self.client.is_connected:
            return False
        
        data = (line + '\n').encode('utf-8')
        try:
            await self.client.write_gatt_char(CHAR_CMD_RX_UUID, data)
            print(f">> {line}")
            return True
        except Exception as e:
            print(f"Send error: {e}")
            return False
    
    async def send_speaker_list(self):
        """Send example speaker list"""
        await self.send_line("SPK BEGIN")
        await asyncio.sleep(0.1)
        await self.send_line("SPK ITEM 0 Speakers")
        await asyncio.sleep(0.1)
        await self.send_line("SPK ITEM 1 Headphones")
        await asyncio.sleep(0.1)
        await self.send_line("SPK END")
        await asyncio.sleep(0.1)
        await self.send_line("SPK CUR 0")
    
    async def disconnect(self):
        """Disconnect from device"""
        if self._keepalive_task:
            self._keepalive_task.cancel()
            self._keepalive_task = None
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("Disconnected")
    
    async def run(self):
        """Main loop"""
        if not await self.connect():
            return
        
        print("\nBLE bridge running. Commands:")
        print("  vol <0-100> - Set volume")
        print("  mute <0/1>  - Set mute state")
        print("  quit        - Exit")
        print()
        
        try:
            while True:
                # Read user input (non-blocking)
                if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                    cmd = sys.stdin.readline().strip()
                    if cmd == "quit":
                        break
                    elif cmd.startswith("vol "):
                        vol = cmd.split()[1]
                        await self.send_line(f"VOL {vol}")
                    elif cmd.startswith("mute "):
                        mute = cmd.split()[1]
                        await self.send_line(f"MUTE {mute}")
                
                await asyncio.sleep(0.1)
        except KeyboardInterrupt:
            print("\nInterrupted")
        finally:
            await self.disconnect()


async def main():
    client = SongLedBleClient()
    await client.run()


if __name__ == "__main__":
    # For Windows, need to set event loop policy
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nExiting...")
