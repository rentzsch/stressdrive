### About

`stressdrive` is a linux and Mac OS X command-line tool meant to verify correct operation of a drive. It does so by filling a drive up with random data and ensuring all the data can be correctly read back.

It was written to verify correct operation of [de-duping SSDs](http://storagemojo.com/2011/06/27/de-dup-too-much-of-good-thing/), but it can be used with normal HDDs or any rewritable block storage device.

**DANGER:** `stressdrive` will overwrite, without warning, all data on the given drive. Be sure to double-check the drive you're aiming it at (Disk Utility.app > Select Drive > Info > Disk Identifier).

### Building

#### Mac OS X

	xcodebuild

or (you can use `cc` or `clang` instead of gcc):

	gcc stressdrive.c -o stressdrive -lcrypto -framework IOKit -framework CoreServices

openssl bundled with os will produce lots of deprecation warnings, so you can use different openssl:

	gcc stressdrive.c -o stressdrive -lcrypto -framework IOKit -framework CoreServices -I/PREFIX/include -L/PREFIX/lib

#### Ubuntu

	sudo apt-get install libssl-dev # You will need openssl headers

	gcc stressdrive.c -o stressdrive -std=c99 -lcrypto

### Usage

	sudo ./stressdrive /dev/rdiskN

### Sample Run

	$ sudo ./stressdrive /dev/rdisk123
	blockSize: 512
	blockCount: 468862128
	speedScale: 16x
	scaled blockSize: 8192
	scaled blockCount: 29303883
	writing random data to /dev/rdisk123
	writing 100% (block 29303002 of 29303883)
	1779f30a231c1d07c578b0e4ee49fde159210d95 <= SHA-1 of written data
	verifying written data
	reading 100% (block 29302306 of 29303883)
	1779f30a231c1d07c578b0e4ee49fde159210d95 <= SHA-1 of read data
	SUCCESS

That run took about 10 hours on a 240GB SSD.

### Run Only Against Entire, Unmounted, Physical Devices

`stressdrive` should always be run against **entire unmounted physical devices**.

Practically: your device path should always be in the form of `/dev/rdiskX` (not `/dev/rdiskXsX`). stressdrive's results can only be trusted if it was allowed to fill the entire device to the device's advertised information-theoretic maximum.

Imagine pointing stressdrive at just a logical partition. If the drive failed during the test it's possible to get back a clean read of the random data just written, while a block outside the device's partition is no longer correct. That would not be an accurate test result.

### "How is this better than Disk Utility's 'Zero Out Data'?"

Some SSD's de-duplicate stored blocks. For these "filling" it with zeros if actually just modifying one or two actual mapping blocks over and over again. It's not a real test of the SSD's hardware.

### "How is this better than Disk Utility's '7-Pass Erase'?"

`stressdrive` only overwrites the drive with data once (so it's 7x faster) and then verifies all the data is correctly read back (which Disk Utility doesn't do at all).

Jens Ayton [informs me](https://twitter.com/ahruman/status/136930141568905217) 7-Pass Erase uses fixed patterns, so de-duping may be an issue there as well.

### "Pshaw! I could do this with dd, /dev/random & shasum!"

Indeed you could. I prefer a minimal focused tool whose operation is fixed, its source simple+readable and offers good built-in progress reporting.

### Portablity

`stressdrive` should be easily portable to other Unixes if anyone wants to do that and toss me a Pull Request.
