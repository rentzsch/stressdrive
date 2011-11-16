### About

`stressdrive` is a Mac OS X command-line tool meant to verify correct operation of a drive. It does so by filling a drive up with random data and ensuring all the data can be correctly read back.

It was written to verify correct operation of [de-duping SSDs](http://storagemojo.com/2011/06/27/de-dup-too-much-of-good-thing/), but it be used with normal HDDs or any rewritable block storage device.

**DANGER:** `stressdrive` will overwrite, without warning, all data on the given drive. Be sure to double-check the drive you're aiming it at (Disk Utility.app > Select Drive > Info > Disk Identifier).

### Usage

	sudo ./stressdrive /dev/rdrive1

### Sample Run

	$ sudo ./stressdrive /dev/rdisk1
	blockSize: 512
	blockCount: 512000
	writing random data to /dev/rdisk1
	writing 100% (block 511360 of 512000)
	2eed7209b7a5b9a1a22cd4eb1b77a59da23c1d56 <= SHA-1 of written data
	verifying written data
	reading 100% (block 510323 of 512000)
	2eed7209b7a5b9a1a22cd4eb1b77a59da23c1d56 <= SHA-1 of read data
	SUCCESS

### "How is this better than Disk Utility's 'Zero Out Data'?"

Some SSD's de-duplicate stored blocks. For these "filling" it with zeros if actually just modifying one or two actual mapping blocks over and over again. It's not a real test of the SSD's hardware.

### "How is this better than Disk Utility's '7-Pass Erase'?"

`stressdrive` only overwrites the drive with data once (so it's 7x faster) and then verifies all the data is correctly read back (which Disk Utility doesn't do at all).

### "Pshaw! I could do this with `dd`, `/dev/random` and `shasum`!"

Indeed you could. I prefer a minimal focused tool whose operation is fixed, its source simple+readable and offers good built-in progress reporting.

### Portablity

`stressdrive` should be easily portable to other Unixes if anyone what to do that and toss me a Pull Request.