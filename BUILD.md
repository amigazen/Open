# Building Open

## Prerequisites

- SAS/C compiler 6.58 or compatible
- NDK 3.2 or compatible
- workbench.library v44 or later
- datatypes.library v45 or later
- icon.library v47 or later (optional, for DefIcons integration)
- SMake (included with SAS/C) or compatible make utility

## Build Instructions

### Using SMakefile

1. Navigate to the Source directory:
```bash
cd Source/
```

2. Build the executable:
```bash
smake
```

This will create the `Open` executable in the Source directory.

3. Install to SDK (optional):
```bash
smake install
```

This copies the executable to `/SDK/C/Open`.

4. Clean build artifacts (optional):
```bash
smake clean
```

## Build Process

The build process:
1. Compiles `open.c` to `open.o` using SAS/C compiler
2. Links `open.o` with `sc:lib/c.o` and required libraries
3. Creates the `Open` executable

## Compiler Options

Compiler options are defined in `SCOPTIONS`:
- `DATA=NEAR` - Near data model
- `CODE=NEAR` - Near code model
- `PARAMETERS=REGISTERS` - Pass parameters in registers
- `NOSTACKCHECK` - Disable stack checking
- `OPTIMIZE` - Enable optimizations
- `UTILITYLIBRARY` - Link with utility.library

## Libraries Required

Runtime libraries:
- `intuition.library` v39+
- `utility.library` v39+
- `workbench.library` v44+
- `datatypes.library` v45+
- `icon.library` v47+ (optional, for DefIcons integration)

## Troubleshooting

### Build Errors

If you encounter build errors:
1. Verify SAS/C compiler is in your path
2. Check that NDK headers are in `include:` assign
3. Ensure all required libraries are available

### Runtime Errors

If the tool fails at runtime:
1. Verify workbench.library v44+ is installed
2. Verify datatypes.library v45+ is installed
3. Check that the file exists and is readable
4. Ensure the file has a recognized datatype or DefIcons rule

