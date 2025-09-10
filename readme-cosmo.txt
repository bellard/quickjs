The executables included in this archive run on Linux, Mac, Windows,
FreeBSD, OpenBSD and NetBSD for both the ARM64 and x86_64
architectures.

Platform Notes:

- if you get errors on Linux, you should disable the binfmt_misc
  module which automatically invokes wine with Windows executable:

sudo sh -c 'echo -1 > /proc/sys/fs/binfmt_misc/cli'     # remove Ubuntu's MZ interpreter
sudo sh -c 'echo -1 > /proc/sys/fs/binfmt_misc/status'  # remove ALL binfmt_misc entries

- Under Windows, you can rename the executables with a .exe extension.

- Use the --assimilate option to build a platform specific binary for
  better startup time:

  ./qjs --assimilate

- See https://github.com/jart/cosmopolitan for more information about
  platform specific issues.
