# Recursive Stacks (librstack)

A dynamically loaded C library implementing recursive stacks. This project was developed to demonstrate advanced memory management, reference counting, and cycle detection in C.

## Overview

The `rstack` library allows for the creation and manipulation of stacks where elements can be either unsigned 64-bit integers (`uint64_t`) or references to other stacks. When a stack is pushed onto another, it is not copied; instead, a reference is added. 

To handle complex data structures, including cyclical stack references, the library implements a custom memory management system using reference counting combined with a mark-and-sweep garbage collection mechanism.

## Features

* **Dynamic Stack Management:** Push and pop integers or stack references.
* **Cycle Handling:** Safe memory management and recursive traversal algorithms that successfully detect and handle cyclic structures.
* **File I/O:** Construct a stack by reading space-separated integers from a valid text file, and serialize the contents of a stack back to a file.
* **Strong Exception Safety:** The library is strictly resistant to memory allocation failures. If `malloc` or related functions fail, no memory leaks occur, and the observable state of the data structures remains unchanged.

## Building the Library

The project requires a C compiler (e.g., GCC) and is built using the provided Makefile. The build process compiles the source files into a shared library named `librstack.so`.

To build the library, run the following command in the root directory:

```bash
make librstack.so
```

To clean the build artifacts, run:

```bash
make clean
```

## Technical Details

* **Language Standard:** Written in C23 (`-std=gnu23`).
* **Memory Testing:** The build process utilizes linker wrappers (`-Wl,--wrap=malloc`, `-Wl,--wrap=free`, etc.) to intercept memory allocation calls. This is used in conjunction with the included testing module to rigorously verify the library's behavior and exception safety under simulated out-of-memory (OOM) conditions.
