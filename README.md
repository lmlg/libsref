# libsref
Scalable reference counting

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

## Authors
Luciano Lo Giudice

Agustina Arzille

## License
Licensed under the GPLv3.
