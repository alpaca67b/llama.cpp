cmake_minimum_required (VERSION 3.16.3)
project (QCC)

if (MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
	add_compile_options("/std:c11;/Ot;/W4;/DQRNG_CMDCTRL_FULL_MODE") #;/DQCC_DEBUG_ENABLE;/DQCC_COMM_DEBUG_ENABLE
else()
	add_compile_options("-std=c11;-O3;-Wall;-Werror;-DQRNG_CMDCTRL_FULL_MODE") #;-DQCC_COMM_DEBUG_ENABLE;-DQCC_DEBUG_ENABLE
endif()

add_library(qcc SHARED)
target_sources(qcc PRIVATE qcc.c)

add_executable (qcc-cli)
target_sources(qcc-cli PRIVATE main.c cargs.c test.c)

# set variable for the C preprocessor to detect the operating system
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(qcc PUBLIC "LINUX_OS")
	target_compile_definitions(qcc-cli PUBLIC "LINUX_OS")
	target_sources(qcc PRIVATE qcc_comm_serial_lin.c)
	target_sources(qcc PRIVATE qcc_comm_tcpudp_lin.c)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_compile_definitions(qcc PUBLIC "WINDOWS_OS")
	target_compile_definitions(qcc-cli PUBLIC "WINDOWS_OS")
	target_sources(qcc PRIVATE qcc_comm_serial_win.c)
	target_sources(qcc PRIVATE qcc_comm_tcpudp_win.c)
	target_link_libraries(qcc wsock32 ws2_32 AdvApi32)
endif()

get_filename_component(PARENT_DIR ../ ABSOLUTE)
target_include_directories(qcc PUBLIC "${PARENT_DIR}/inc")
target_link_libraries(qcc-cli PRIVATE qcc)
