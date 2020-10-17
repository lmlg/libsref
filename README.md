# libsref
This library implements scalable reference counting. Its main purpose is to
reduce contention among multiple threads when using reference counted objects,
at the expense of increased latency.

## Installation
As with most GNU packages, libsref follows a very simple convention:
```shell
./configure
make
make install
```

## Usage
Link with this library (-lsref), and be sure to call the initialization
routine, 'sref_lib_init', before calling any other function from the API.

## Documentation
See doc/API.md and doc/design.md

## Examples
See the files at examples/

## Authors
Luciano Lo Giudice

Agustina Arzille

## License
Licensed under the GPLv3.
