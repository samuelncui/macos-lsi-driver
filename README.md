# Porting FreeBSD LSI MPT2 Driver to macOS (WIP)

I'm trying to port FreeBSD LSI MPT2 Controller (eg lsi2308) driver to macOS, slowly. **This project is not working at all at this moment.**

## Original Readme

[![Build Status](https://travis-ci.org/dukzcry/osx-goodies.svg?branch=master)](https://travis-ci.org/dukzcry/osx-goodies)

I/O Kit driver for LSI MegaRAID SAS family of hardware RAID controllers, which isn't supported by proprietary MegaRAID.kext (PPC) and AppleLSIFusionMPT.kext or AppleRAIDCard.kext (x86). This driver is Xcode project, for macOS.

Here's rough list of cards which should be supported:
- LSI MegaRAID SAS 8xxx, 92xx
- Dell PERC 5, PERC 6, H310, H700, H800
- IBM ServeRAID M1115
- Intel RAID SRCSAS18E, SRCSAS144E

**Except for / definitely unsupported**:
- controllers with non-MegaRAID firmware
- cards based on SAS2208 (Thunderbolt) chip
- Fusion-MPT of various generations, like SAS2308

Note, that probably almost every card from this family requires x86 host w BIOS/EFI to get access to device firmware's management utility (maybe such card will work on PPC machine but without booting capatibility and you'll not be able to do initial setup of things outside of OS, i.e. you'll need to insert card inside of PC to create virtual disks and so on).

Notes for coder:
- Templates are used for interaction with project-independend helper library
- Structures, enumerations and unions are typedefined to raise 'pointer to incomplete type' invisible errors
- Incapsulation ignored
- Checks for DMA buffers bouncing aren't required on OS X, and hence, you'll see no synchronization primitives in code
