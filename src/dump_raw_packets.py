#!/usr/bin/env python3
import struct
import binascii
import sys
import os
import argparse

def main():
    parser = argparse.ArgumentParser(description='Analyze PKLG file for speed commands')
    parser.add_argument('file', help='Path to .pklg file')
    args = parser.parse_args()
    
    filename = args.file
    
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found.")
        sys.exit(1)
        
    print(f"Analyzing {filename}...")

    with open(filename, "rb") as f:
        data = f.read()
        
    # Brute force search for the payload bytes matching known speed commands
    # Example Speed command payload: 02 04 02 09 04 09 02 01 02 32 00 00 44
    target = bytes.fromhex("02040209040902010232000044")
    print(f"Searching for signature {target.hex()}...")
    
    found = data.find(target)
    if found == -1:
        print("No matches found.")
        
    while found != -1:
        print(f"\nFOUND AT OFFSET {found}")
        # Look backwards for context (PacketLogger Header likely nearby)
        start = max(0, found - 64)
        end = min(len(data), found + 32)
        context = data[start:end]
        
        # Print Hex Dump of Context
        print(f"Context: {binascii.hexlify(context).decode('utf-8')}")
        
        found = data.find(target, found + 1)

if __name__ == "__main__":
    main()
