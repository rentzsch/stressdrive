## About

`stressdrive` is a macOS and Linux command-line tool meant to verify correct operation of a drive. It does so by filling a drive up with random data and ensuring all the data can be correctly read back.

It was written to verify correct operation of [de-duping SSDs](http://storagemojo.com/2011/06/27/de-dup-too-much-of-good-thing/), but it can be used with normal HDDs or any rewritable block storage device.

**DANGER:** `stressdrive` will overwrite, without warning, all data on the given drive. Be sure to double-check the drive you're aiming it at (`diskutil list` or Disk Utility.app > Select Drive > Info > Disk Identifier).

## Usage

	sudo ./stressdrive /dev/rdiskN

### Run Only Against Entire, Unmounted, Physical Devices

`stressdrive` should always be run against **entire unmounted physical devices**.

Practically: your device path should always be in the form of `/dev/rdiskX` (not `/dev/rdiskXsX`). stressdrive's results can only be trusted if it was allowed to fill the entire device to the device's advertised information-theoretic maximum.

Imagine pointing stressdrive at just a logical partition. If the drive failed during the test it's possible to get back a clean read of the random data just written, while a block outside the device's partition is no longer correct. That would not be an accurate test result.

## Sample Run

Here's stressdrive running against a 2 GB USB Flash drive:

	$ sudo ./stressdrive /dev/rdisk999
	Password:
	disk block size: 512
	disk block count: 3948424
	buffer size: 8388608
	succesfully created no idle assertion
	writing random data to /dev/rdisk999
	writing 100.0% (3948424 of 3948424) 00:03:54
	6519594c7bf64d5e4e087cfbc5ba6324d25e8c0d <= SHA-1 of written data
	verifying written data
	reading 100.0% (3948424 of 3948424) 00:01:24
	6519594c7bf64d5e4e087cfbc5ba6324d25e8c0d <= SHA-1 of read data
	SUCCESS
	succesfully released no idle assertion

## Building

### macOS

First, you'll need OpenSSL, which you should install via homebrew:

	brew install openssl

Then you can just:

	xcodebuild

Or compile it directly:

	gcc stressdrive.c -o stressdrive -framework IOKit -framework CoreServices -I/usr/local/opt/openssl/include /usr/local/opt/openssl/lib/libcrypto.a

### Ubuntu

	sudo apt-get install libssl-dev # You will need openssl headers

	gcc stressdrive.c -o stressdrive -std=c99 -lcrypto

## FAQ

### "How is this better than Disk Utility's 'Zero Out Data'?"

Some SSD's de-duplicate stored blocks. For these "filling" it with zeros if actually just modifying one or two actual mapping blocks over and over again. It's not a real test of the SSD's hardware.

### "How is this better than Disk Utility's '7-Pass Erase'?"

`stressdrive` only overwrites the drive with data once (so it's 7x faster) and then verifies all the data is correctly read back (which Disk Utility doesn't do at all).

Jens Ayton [informs me](https://twitter.com/ahruman/status/136930141568905217) 7-Pass Erase uses fixed patterns, so de-duping may be an issue there as well.

### "Pshaw! I could do this with dd, /dev/random & shasum!"

Indeed you could. I prefer a minimal focused tool whose operation is fixed, its source simple+readable and offers good built-in progress reporting.

## Version History

### v1.3: 2018-02-19

- [NEW] Display speed alongside progress. ([Ivan Kuchin](https://github.com/rentzsch/stressdrive/pull/9))

### v1.2.1: 2018-01-04 [download](https://github.com/rentzsch/stressdrive/releases/download/1.2.1/stressdrive-mac-1.2.1.zip)

- [FIX] Statically link libcrypto. ([rentzsch](https://github.com/rentzsch/stressdrive/commit/30eac57352c49d3ebf8d980f12b3369b316f5c97))

### v1.2: 2018-01-03 [download](https://github.com/rentzsch/stressdrive/releases/download/1.2/stressdrive-mac-1.2.zip)

- [NEW] Linux support. ([Ivan Kuchin](https://github.com/rentzsch/stressdrive/pull/8))
- [NEW] Better progress display: elapsed time and ETA. ([Ivan Kuchin](https://github.com/rentzsch/stressdrive/pull/8))
- [NEW] Use AES 128 CBC with a random key and initialization vector as a much faster source of data sans fixed patterns.  ([Ivan Kuchin](https://github.com/rentzsch/stressdrive/pull/8))
- [NEW] Don't allow the Mac to idle (sleep) while running.  ([Ivan Kuchin](https://github.com/rentzsch/stressdrive/pull/8))
- [NEW] Print version alongside usage. ([rentzsch](https://github.com/rentzsch/stressdrive/commit/77253b193308b0670209fa9801d2ecb851a811b6))
- [CHANGE] Remove speed scaling in favor of a simpler and as fast fixed 8MB copy buffer. ([Ivan Kuchin](https://github.com/rentzsch/stressdrive/pull/8))
- [FIX] Possible overflow in speedscale. ([Doug Russell](https://github.com/rentzsch/stressdrive/pull/3))
- [FIX] Xcode project references Homebrew's OpenSSL in a non-version-specific way (so it doesn't break on every update). ([rentzsch](https://github.com/rentzsch/stressdrive/commit/7575853194793d3ee718252f08a7af52853f5424))

### v1.1: 2011-11-17 [download](https://github.com/rentzsch/stressdrive/archive/1.1.zip)

- [NEW] Speed scaling, which increases the copy buffer to the maximum that's still evenly divisible by the drive's capacity. ([rentzsch](https://github.com/rentzsch/stressdrive/commit/a3f4598af5f9957100613ff66240628bb0ab2078))

### v1.0: 2011-11-16 [download](https://github.com/rentzsch/stressdrive/archive/1.0.zip)

- Initial release.
