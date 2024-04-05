## memtalk

Credit to Julian Goldstein from virt.so for giving me the following prompt as a take-home assignment.

### NAME
  
  `memtalk` - A utility for two processes to talk to each other via a POSIX shared memory object.

### SYNOPSIS
  
  `memtalk [-f Path to a POSIX shared memory object]`

### DESCRIPTION

  `memtalk` is a process that can talk to another `memtalk` processes through a POSIX shared memory
  object.

  `memtalk` functions in the following way:

  - When `memtalk` is executed, it checks its `-f` argument for a valid path to a POSIX shared
    memory object. If the `-f` argument is missing, the program returns -1.

  - If the POSIX shared memory object passed to it via the `-f` argument does not currently exist
    on the filesystem, memtalk creates it, before opening it.

  - memtalk then uses the resulting file descriptor to map in a page of memory into
    into its address space that has shared permissions. The size of this allocation is determined
	via `sysconf(3)`.

  - Once the shared page has been mapped, if memtalk is the one that created the POSIX shared
    memory object, it writes a 32-bit magic number to the base address to signify that 
    it has initialized the memory.
  
  - Memory is structured like so:
```
  +--------------------+
  |  32-bit magic num  |
  +--------------------+
  | ring buffer struct |
  +--------------------+
  |  ring buffer slab  |
  +--------------------+
  | ring buffer struct |
  +--------------------+
  |  ring buffer slab  |
  +--------------------+
```
- One ring buffer is used by the first instance of memtalk to send data to the second instance of memtalk and the other is used by the second instance of memtalk to send data to the first instance.

- `memtalk` reads stdin and forwards the bytes to its sending ring buffer and writes the receiving ring buffer's bytes to stdout.

### EXAMPLE USAGE

Open up terminal 1 and write:

```bash
$ cat /usr/share/dict/words | memtalk -f /someobject
```

Then open up terminal 2 and write:

```bash
$ find / | memtalk -f /someobject
```