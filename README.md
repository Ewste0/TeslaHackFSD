# Tigard Maps Installer

A Qt 6 desktop utility for working with a Tigard/FT2232H adapter and supported
target hardware.

The application contains no account system, license server, HWID collection,
telemetry, access requests, or heartbeat. Its network functions use only the
configured local target addresses and the local HTTP server required by the
installation workflow.

## Safety

This program can read, erase, and write SPI flash. Incorrect wiring, voltage,
addresses, images, or interrupted writes can permanently damage the target.
Make verified backups first and use the software only on hardware you own or
are authorized to service.

This independent project is not affiliated with Tesla or Tigard.

## Hacked maps for EU with FSD 
https://anonfilesnew.com/s/HFxrOcxnp-h

## Requirements

- Tigard or another compatible FT2232H adapter (`VID 0403`, `PID 6010`).
- Qt 6.5 or newer with MinGW on Windows.
- CMake and Ninja.
- `libftdi1` and `libusb-1.0` from MSYS2 MinGW64.
- WinUSB on both FTDI interfaces when running on Windows.

## Windows build

From PowerShell:

```powershell
./scripts/build-windows.ps1
```

The portable application is produced in `dist/`.

## Linux build

Install Qt 6, CMake, a C++17 compiler, `pkg-config`, `libftdi1`, and
`libusb-1.0`, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Data files

ENC-region backups are stored as raw binary data. No maps, hardware dumps,
vehicle identifiers, certificates, customer projects, private keys, or
production secrets are included.

## License

GNU GPL v3 or later. See [LICENSE](LICENSE).
