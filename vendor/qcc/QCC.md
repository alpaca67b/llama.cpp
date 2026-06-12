# QCC QRNG Cmd&Ctrl C implementation

## QCC library
QRNG Cms&Ctrl C11 portable library to communicate with a Crypta Labs QRNG device, it can be used in Windows and Linux OS.

### Communication interface
QCC supports the following communication interfaces:
- **USB (Serial)**: command and data on a single serial interface, usually USB-serial so no baudrate setting needed.
- **TCP/UDP**: Command sent on TCP (QRNG device port 54936), data received on UDP port 54936, Reserve the port 54936 for the QRNG usage.

## QCC cli
Command line interface application to test all the QCC capabilities, it can also write the random numbers to a file.
The source code can be sued as an example on how to use the QCC library.

## Build 
[Cmake](https://cmake.org/) is used to build the library (libqcc.so or qcc.dll) and the cli executable under Linux and Windows, move to qcc directory and run:
```
cmake -S . -B build
cmake --build build
```
It will generate the toolchain files and the final artifacts the build directory
The whole build configuration is defined in the file CMakeLists.txt
**NOTE**: On some system cmake 3.xx command is cmake3 

### Requirements Linux
- make
- cmake (min version 3.16.3)

### Requirements Windows
- Visual Studio, MSVC compiler
- cmake

## Developer manual
You can include the QCC library source code and compile together with your application, or build the library and statically/dynamically link it with your application. 

### Structure packing
When using a compiler different that gcc or MSVC take care of the correct packing of the data structure defined in cmdctrl_types.h.
If your compiler doesn't support `#pragma pack(1)` modify the file to be compatible with your compiler and perform the correct packing.

### Debug messages
To enable debug messages on stdout the following should be defined:
- `QCC_DEBUG_ENABLE`: qcc core debug messages enable.
- `QCC_COMM_DEBUG_ENABLE`: communication interface debug messages enable.

These defines should be added to the CMakeLists.txt file and rerun the whole building procedure.

## Deployment manual
### 54936 UDP port reservation
The OS automatically assing listening UDP port to an ephemeral range, the port 54936 should be reserved so the only applications that can open it are the one that use QCC.

On Linux:
```
sysctl -w net.ipv4.ip_local_reserved_ports=54936
```

On Windows:
```
netsh int ipv4 add excludedportrange protocol=udp startport=54936 numberofports=1
```
### Windows serial port name
If the serial port is grater than COM9 the name of the com port passed to the open fuinction must be in the form `\\.\COM10` as explained in this Microsoft [HOWTO](https://support.microsoft.com/en-us/topic/howto-specify-serial-ports-larger-than-com9-db9078a5-b7b6-bf00-240f-f749ebfd913e).

## API Reference

### Return code:
Defined in qcc_errno.h, all the APIS return one of the following integer code:
- `QCC_OK`
- `QCC_ERROR`
- `QCC_TIMEOUT`
- `QCC_INVALID_ARGUMENT`
- `QCC_MALLOC_ERROR`
- `QCC_NACK`

### QCC types
 - `qcc_hdl_t`: main handle, all the device APIs need this, initialized by the `qcc_init()` function
 - `qcc_comm_type_t`: communication interface type

### Cmd&Ctrl types
They represent the requests and responses of the Cmd&Ctrl protocol:
- `cmdctrl_start_mode_t`: one shot or continuous mode
- `cmdctrl_status_t`: QRNG status, error conditions and number of ready bytes
- `cmdctrl_config_t`: QRNG configuration
- `cmdctrl_statistics_t`: QRNG Statistics
- `cmdctrl_info_t`: Serial number, software and hardware version

### int qcc_version(void);
Get QCC library version.

### int qcc_init(qcc_hdl_t *hdl, qcc_comm_type_t comm, char *dev_id, int timeout_ms, int read_size)
Initialize QCC device and link to a specific communication handler
- `hdl` : qcc handle to initialize
- `comm` : communication type, `QCC_SERIAL` or `QCC_TCPUDP`
- `dev_id` : device identification, e.g "COM1", "/dev/ttyACM0", "192.168.1.34"
- `timeout_ms`: timeout used in the communication with the device
- `read_size` : maximum number of bytes to read in one go, depends on the communication interface used and the overall speed of the link.

### int qcc_cmd_get_status(qcc_hdl_t *hdl, cmdctrl_status_t *sts)
Read QRNG status
- `hdl` : qcc handle
- `sts` : pointer to a `cmdctrl_status_t` structure filled with the status received

### int qcc_cmd_start(qcc_hdl_t *hdl, cmdctrl_start_mode_t mode, uint8_t *buf, uint16_t len)
Start command,
- `hdl` : qcc handle
- `mode`: start mode, `QCC_ONE_SHOT` to read a chunk of data straight away or `QCC_CONTINUOUS` to start the continuous mode
- `buf` : pointer to a pre-allocated buffer to fill with the data received (only if `QCC_ONE_SHOT`)
- `len` : How many bytes to read (only if `QCC_ONE_SHOT`), limited by the currently available bytes

### int qcc_read_continuous(qcc_hdl_t *hdl, uint8_t *buf, int len)
Read data from a QRNG device currently in continuous mode, start continuous mode before using this function
- `hdl` : qcc handle
- `buf` : pointer to a pre-allocated buffer to fill with the data received 
- `len` : How many bytes to read, limited to 2GB (if you system can allocate a buffer that big)

### int qcc_cmd_stop(qcc_hdl_t *hdl)
Stop continuous mode
- `hdl` : qcc handle

### int qcc_cmd_reset(qcc_hdl_t *hdl)
Reset Random number generation
- `hdl` : qcc handle

### int qcc_cmd_set_config(qcc_hdl_t *hdl, cmdctrl_config_t *cfg)
Set QRNG configuration
- `hdl` : qcc handle
- `cfg` : pointer to a valid `cmdctrl_config_t` structure

### int qcc_cmd_get_config(qcc_hdl_t *hdl, cmdctrl_config_t *cfg)
Get QRNG configuration
- `hdl` : qcc handle
- `cfg` : pointer to `cmdctrl_config_t` structure to fill with the configuration received

### int qcc_cmd_get_statistics(qcc_hdl_t *hdl, cmdctrl_statistics_t *stats)
Get QRNG statistics
- `hdl` : qcc handle
- `stats` : pointer to `cmdctrl_statistics_t` structure to fill with the statistics received

### int qcc_cmd_get_info(qcc_hdl_t *hdl, cmdctrl_info_t *info)
Get QRNG device information
- `hdl` : qcc handle
- `info` : pointer to `cmdctrl_info_t` structure to fill with the information received

### int qcc_close(qcc_hdl_t *hdl)
Close QCC handle 
- `hdl` : qcc handle

## Notes
- The QRNG sends data in little-endian format, when using a different host machine it might be necessary to account for that.
- Stopping the continuous mode can take sometimes when using the serial interface, depends on the timeout used.