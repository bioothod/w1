# w1
Linux kernel w1 netlink client

Linux kernel w1 netlink documentation: https://www.kernel.org/doc/Documentation/w1/w1.netlink
w1 over netlink can be used to work with w1 hardware (both master and slave devices) from userspace
with noticebly lower latencies than via sysfs files.


This source be used as an example on how to bind to netlink connector socket,
how to create messages and parse replies.

Protocol supports both master and slave commands.
Master supports device listing, slave search, bus reset and generic w1 read/write/touch operations.
You can also manually add/remove slave device ID.

Slave commands are device specific.

To build the binary you have to edit `Makefile` and specify include path to w1 kernel drivers,
in particular you need path to directory where `w1_netlink.h` file is located.

For example add this string to `INCLUDES` option if your linux kernel source tree (only w1 headers are actually needed) lives at `/lib/modules/4.0.8-200.fc21.x86_64/source/`:
```
-I/lib/modules/4.0.8-200.fc21.x86_64/source/drivers/w1
```
