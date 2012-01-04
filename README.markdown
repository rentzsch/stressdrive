### About

Stressdrive is a Mac OS X command-line tool meant to verify correct operation of a drive. It does so by filling a drive up with random data and ensuring all the data can be correctly read back.

It was written to verify correct operation of [de-duping SSDs](http://storagemojo.com/2011/06/27/de-dup-too-much-of-good-thing/), but it can be used with normal HDDs or any rewritable block storage device.

**DANGER:** stressdrive will overwrite, without warning, all data on the given drive. Be sure to double-check the drive you're aiming it at (Disk Utility.app > Select Drive > Info > Disk Identifier).

### Usage

	sudo ./stressdrive /dev/rdrive1

### Sample Run

	$ sudo ./stressdrive /dev/rdisk9
	blockSize: 512
	blockCount: 468862128
	speedScale: 16x
	scaled blockSize: 8192
	scaled blockCount: 29303883
	writing random data to /dev/rdisk0
	writing 100% (block 29303002 of 29303883)
	1779f30a231c1d07c578b0e4ee49fde159210d95 <= SHA-1 of written data
	verifying written data
	reading 100% (block 29302306 of 29303883)
	1779f30a231c1d07c578b0e4ee49fde159210d95 <= SHA-1 of read data
	SUCCESS

That run took about 10 hours on a 240GB SSD.

### "How is this better than Disk Utility's 'Zero Out Data'?"

Some SSD's de-duplicate stored blocks. For these "filling" it with zeros if actually just modifying one or two actual mapping blocks over and over again. It's not a real test of the SSD's hardware.

### "How is this better than Disk Utility's '7-Pass Erase'?"

Stressdrive only overwrites the drive with data once (so it's 7x faster) and then verifies all the data is correctly read back (which Disk Utility doesn't do at all).

Jens Ayton [informs me](https://twitter.com/ahruman/status/136930141568905217) 7-Pass Erase uses fixed patterns, so de-duping may be an issue there as well.

### "Pshaw! I could do this with dd, /dev/random & shasum!"

Indeed you could. I prefer a minimal focused tool whose operation is fixed, its source simple+readable and offers good built-in progress reporting.

### Portablity

Stressdrive should be easily portable to other Unixes if anyone wants to do that and toss me a Pull Request.