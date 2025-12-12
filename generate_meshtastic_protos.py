#!/usr/bin/env python3
"""
Generate Meshtastic Protobuf C Headers

This script downloads the necessary .proto files from the Meshtastic protobufs
repository and generates C header files using nanopb.

Requirements:
    pip install nanopb protobuf grpcio-tools

Usage:
    python generate_meshtastic_protos.py
"""

import os
import sys
import subprocess
import urllib.request
import shutil
from pathlib import Path

# Meshtastic protobufs repository
PROTO_REPO = "https://raw.githubusercontent.com/meshtastic/protobufs/master"

# Proto files we need for basic Position and Text messaging
REQUIRED_PROTOS = [
    "meshtastic/mesh.proto",
    "meshtastic/portnums.proto",
    "meshtastic/channel.proto",
    "meshtastic/config.proto",
    "meshtastic/module_config.proto",
    "meshtastic/telemetry.proto",
    "meshtastic/device_ui.proto",
    "meshtastic/xmodem.proto",
]

# Output directory
OUTPUT_DIR = Path("lib/meshtastic")

def download_proto(proto_file, output_dir):
    """Download a .proto file from GitHub"""
    url = f"{PROTO_REPO}/{proto_file}"
    output_path = output_dir / proto_file

    print(f"Downloading {proto_file}...")

    # Create directory if needed
    output_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        urllib.request.urlretrieve(url, output_path)
        print(f"  OK Saved to {output_path}")
        return True
    except Exception as e:
        print(f"  ERROR: {e}")
        return False

def check_nanopb():
    """Check if nanopb generator is available"""
    try:
        # Find nanopb installation
        import nanopb
        nanopb_dir = Path(nanopb.__file__).parent
        generator_script = nanopb_dir / "generator" / "nanopb_generator.py"

        if generator_script.exists():
            print(f"OK nanopb generator found: {generator_script}")
            return str(generator_script)
    except Exception as e:
        pass

    print("ERROR: nanopb generator not found")
    print("\nInstall with:")
    print("  pip install nanopb protobuf grpcio-tools")
    return None

def generate_headers(proto_dir, output_dir, generator_script):
    """Generate C headers using nanopb"""
    print("\nGenerating C headers...")

    # Find all .proto files
    proto_files = list(proto_dir.glob("**/*.proto"))

    if not proto_files:
        print("ERROR: No .proto files found!")
        return False

    success = True
    for proto_file in proto_files:
        print(f"Processing {proto_file.name}...")

        try:
            # Run nanopb generator directly with include path
            result = subprocess.run(
                [
                    "python", generator_script,
                    "--output-dir", str(output_dir),
                    "-I", str(proto_dir),
                    str(proto_file)
                ],
                capture_output=True,
                text=True
            )

            if result.returncode == 0:
                print(f"  OK Generated headers")
            else:
                print(f"  ERROR: {result.stderr}")
                success = False

        except Exception as e:
            print(f"  ERROR: {e}")
            success = False

    return success

def create_nanopb_options(temp_dir):
    """Create nanopb options file for mesh.proto"""
    options_content = """# Nanopb options for Meshtastic mesh.proto
# Limits string and bytes field sizes for embedded systems

# Default limits
*String max_size:64
*bytes max_size:256

# Specific overrides for payload fields (use fixed-size arrays instead of callbacks)
meshtastic.Data.payload max_size:256
meshtastic.MeshPacket.encrypted max_size:256
"""

    # Create options file in temp directory alongside mesh.proto
    options_file = temp_dir / "meshtastic" / "mesh.options"
    options_file.write_text(options_content)
    print(f"OK Created {options_file}")

def main():
    print("=" * 60)
    print("Meshtastic Protobuf Header Generator")
    print("=" * 60)

    # Check for nanopb
    generator_script = check_nanopb()
    if not generator_script:
        return 1

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"\nOK Output directory: {OUTPUT_DIR}")

    # Download proto files
    print("\n" + "=" * 60)
    print("Downloading .proto files...")
    print("=" * 60)

    temp_dir = Path("temp_protos")
    temp_dir.mkdir(exist_ok=True)

    download_success = True
    for proto in REQUIRED_PROTOS:
        if not download_proto(proto, temp_dir):
            download_success = False

    if not download_success:
        print("\nERROR: Failed to download some proto files")
        return 1

    # Create nanopb options for mesh.proto
    create_nanopb_options(temp_dir)

    # Generate headers
    print("\n" + "=" * 60)
    print("Generating C headers with nanopb...")
    print("=" * 60)

    if not generate_headers(temp_dir, OUTPUT_DIR, generator_script):
        print("\nERROR: Failed to generate some headers")
        return 1

    # Cleanup
    shutil.rmtree(temp_dir)
    print(f"\nOK Cleaned up temporary files")

    # List generated files
    print("\n" + "=" * 60)
    print("Generated files:")
    print("=" * 60)

    for header in sorted(OUTPUT_DIR.glob("*.h")):
        print(f"  OK {header.name}")

    print("\n" + "=" * 60)
    print("SUCCESS!")
    print("=" * 60)
    print("\nNext steps:")
    print("1. Remove #define MESHTASTIC_PROTOBUF_DISABLED from meshtastic_interface.cpp")
    print("2. Uncomment the protobuf includes")
    print("3. Rebuild with: pio run")
    print("\nGenerated headers location: lib/meshtastic/")

    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nCancelled by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n\nERROR: Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
