# w1
Linux kernel w1 netlink client

Linux kernel w1 documentation: https://www.kernel.org/doc/Documentation/w1/w1.netlink

This source be used as an example on how to bind to netlink connector socket,
how to create messages and parse replies.

Protocol supports both master and slave commands.
Master supports device listing, slave search, bus reset and generic w1 read/write/touch operations.
You can also manually add/remove slave device ID.

Slave commands are device specific.
