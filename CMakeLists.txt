# Defines Esher-Core library target.
project( Esher-Core LANGUAGES CXX C)
cmake_minimum_required( VERSION 3.2 FATAL_ERROR )

set( BLOCKCHAIN_NAME "Testnet" )

set( CLI_CLIENT_EXECUTABLE_NAME graphene_client )
set( GUI_CLIENT_EXECUTABLE_NAME Esher-Core )
set( CUSTOM_URL_SCHEME "gcs" )
set( INSTALLER_APP_ID "68ad7005-8eee-49c9-95ce-9eed97e5b347" )

set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
  set( CMAKE_CXX_EXTENSIONS ON ) # for __int128 support
else()
  set( CMAKE_CXX_EXTENSIONS OFF )
endif()

# http://stackoverflow.com/a/18369825
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must be at least 4.8!")
    endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang version must be at least 3.3!")
    endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    if ("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "19.0")
      message(FATAL_ERROR "MSVC version must be at least 19.0 (Visual Studio 2015 Update 1)!")
    endif()

    # allow MSVC VS2015 with Update 1, other 2015 versions are not supported
    if ("${CMAKE_CXX_COMPILER_VERSION}" VERSION_EQUAL "19.0")
        if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "19.0.23506.0")
            message(FATAL_ERROR "Your version ${CMAKE_CXX_COMPILER_VERSION} of MSVC is not supported, use version 19.0.23506.0 (Visual Studio 2015 Update 1)!")
        endif()
    endif()
endif()

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules" )

include(CheckCCompilerFlag)
include(Utils)

# function to help with cUrl
macro(FIND_CURL)
   if (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
      find_package(OpenSSL REQUIRED)
      set (OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
      set (CMAKE_FIND_LIBRARY_SUFFIXES .a)
      find_package(CURL REQUIRED)
      list(APPEND CURL_LIBRARIES ${OPENSSL_LIBRARIES} ${BOOST_THREAD_LIBRARY} ${CMAKE_DL_LIBS})
      set (CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})
   else (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
      find_package(CURL REQUIRED)
   endif (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)

   if( APPLE AND NOT "${CURL_VERSION_STRING}" VERSION_LESS "7.77.0" )
      list( APPEND CURL_LIBRARIES "-framework CoreFoundation" )
      list( APPEND CURL_LIBRARIES "-framework SystemConfiguration" )
   endif()

   message(STATUS "CURL libraries: ${CURL_LIBRARIES}")

   if( WIN32 )
     if ( MSVC )
       list( APPEND CURL_LIBRARIES Wldap32 )
     endif( MSVC )

     if( MINGW )
       # MinGW requires a specific order of included libraries ( CURL before ZLib )
       find_package( ZLIB REQUIRED )
       list( APPEND CURL_LIBRARIES ${ZLIB_LIBRARY} pthread )
     endif( MINGW )

     list( APPEND CURL_LIBRARIES ${PLATFORM_SPECIFIC_LIBS} )
   endif( WIN32 )
endmacro()

# Save the old value of CMAKE_REQUIRED_FLAGS
set( TEMP_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS} )

# Check submodules
if(NOT MANUAL_SUBMODULES)
  find_package(Git)
  if(GIT_FOUND)
    function (check_submodule relative_path)
      execute_process(COMMAND git rev-parse "HEAD" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${relative_path} OUTPUT_VARIABLE localHead)
      execute_process(COMMAND git rev-parse "HEAD:${relative_path}" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE checkedHead)
      string(COMPARE EQUAL "${localHead}" "${checkedHead}" upToDate)
      if (upToDate)
        message(STATUS "Submodule '${relative_path}' is up-to-date")
      else()
        message(FATAL_ERROR "Submodule '${relative_path}' is not up-to-date. Please update all submodules with\ngit submodule update --init --recursive --force\nor run cmake with -DMANUAL_SUBMODULES=1\n")
      endif()
    endfunction ()

    message(STATUS "Checking submodules")
    check_submodule(docs)
    check_submodule(libraries/fc)
  endif()
endif()
# Make sure to always re-run the test.
unset( MANUAL_SUBMODULES CACHE )

# Fortify source
if (CMAKE_COMPILER_IS_GNUCXX)
	if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		message (STATUS "Setting optimizations for clang++")
		set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1")
		set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1 -g")

		# check and add data execution prevention
		message (STATUS "Enabling data execution prevention")
		add_linker_flag("-fsanitize=safe-stack")

		# check and add Stack-based buffer overrun detection
		set(CMAKE_REQUIRED_FLAGS "-fstack-protector")
		check_c_compiler_flag("" HAVE_STACKPROTECTOR)
		if(HAVE_STACKPROTECTOR)
			message (STATUS "Enabling stack-based buffer overrun detection")
			add_flag_append(CMAKE_C_FLAGS "-fstack-protector")
			add_flag_append(CMAKE_CXX_FLAGS "-fstack-protector")
		endif()
	else ()
		message (STATUS "Setting optimizations for g++")
		set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1")
		set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1 -g")

		# check and add data execution prevention
		set(CMAKE_REQUIRED_FLAGS "-Wl,-znoexecstack")
		check_c_compiler_flag("" HAVE_NOEXECSTACK)
		if(HAVE_NOEXECSTACK)
			message (STATUS "Enabling data execution prevention")
			add_linker_flag("-znoexecstack")
		endif()

		# check and add Stack-based buffer overrun detection
		set(CMAKE_REQUIRED_FLAGS "-fstack-protector-strong")
		check_c_compiler_flag("" HAVE_STACKPROTECTOR)
		if(HAVE_STACKPROTECTOR)
			message (STATUS "Enabling stack-based buffer overrun detection")
			add_flag_append(CMAKE_C_FLAGS "-fstack-protector-strong")
			add_flag_append(CMAKE_CXX_FLAGS "-fstack-protector-strong")
		endif()

	endif ()
endif ()

# check for Data relocation and Protection (RELRO)
set(CMAKE_REQUIRED_FLAGS "-Wl,-zrelro,-znow")
check_c_compiler_flag("" HAVE_RELROFULL)
if(HAVE_RELROFULL)
	message (STATUS "Enabling full data relocation and protection")
	add_linker_flag("-zrelro")
	add_linker_flag("-znow")
else()
	#if full relro is not available, try partial relro
	set(CMAKE_REQUIRED_FLAGS "-Wl,-zrelro")
	check_c_compiler_flag("" HAVE_RELROPARTIAL)
	if(HAVE_RELROPARTIAL)
		message (STATUS "Enabling partial data relocation and protection")
		add_linker_flag("-zrelro")
	endif()
endif()

set(CMAKE_REQUIRED_FLAGS ${TEMP_REQUIRED_FLAGS} )

# position independent executetable (PIE)
# position independent code (PIC)
if (NOT MSVC)
    add_definitions (-fPIC)
endif(NOT MSVC)

set(CMAKE_EXPORT_COMPILE_COMMANDS "ON")
set( GRAPHENE_EGENESIS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/libraries/egenesis/genesis.json"
     CACHE STRING "Path to embedded genesis file" )

if (USE_PCH)
  include (cotire)
endif(USE_PCH)

option(USE_PROFILER "Build with GPROF support(Linux)." OFF)

# Use Boost config file from fc
set(Boost_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/fc/CMakeModules/Boost")

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libraries/fc/GitVersionGen" )
include( GetGitRevisionDescription )
get_git_head_revision( GIT_REFSPEC GIT_SHA2 )

SET(BOOST_COMPONENTS)
LIST(APPEND BOOST_COMPONENTS thread
                             iostreams
                             date_time
                             system
                             filesystem
                             program_options
                             chrono
                             unit_test_framework
                             context
                             coroutine
                             regex)
# boost::endian is also required, but FindBoost can't handle header-only libs
SET( Boost_USE_STATIC_LIBS ON CACHE STRING "ON or OFF" )

IF(WIN32)
   if($ENV{BOOST_ROOT})
       SET(BOOST_ROOT $ENV{BOOST_ROOT})
   endif($ENV{BOOST_ROOT})
  set(Boost_USE_MULTITHREADED ON)
  set(BOOST_ALL_DYN_LINK OFF) # force dynamic linking for all libraries
  add_definitions("-DCURL_STATICLIB")
  list(APPEND PLATFORM_SPECIFIC_LIBS ws2_32 crypt32 mswsock userenv )
ELSE( WIN32 )
   IF( APPLE )
      set( CMAKE_THREAD_LIBS_INIT "-lpthread" )
      set( CMAKE_HAVE_THREADS_LIBRARY 1 )
      set( CMAKE_USE_WIN32_THREADS_INIT 0 )
      set( CMAKE_USE_PTHREADS_INIT 1 )
      set( THREADS_PREFER_PTHREAD_FLAG ON )
   ENDIF( APPLE )
ENDIF(WIN32)

FIND_PACKAGE(Boost CONFIG REQUIRED COMPONENTS ${BOOST_COMPONENTS})

# enforce more strict compiler warnings and errors
add_compiler_flag_if_available("-Wall")
add_compiler_flag_if_available("-Wclobbered")
add_compiler_flag_if_available("-Wempty-body")
add_compiler_flag_if_available("-Wformat-security")
add_compiler_flag_if_available("-Wignored-qualifiers")
add_compiler_flag_if_available("-Wimplicit-fallthrough=5")
add_compiler_flag_if_available("-Wmissing-field-initializers")
add_compiler_flag_if_available("-Wpointer-arith")
add_compiler_flag_if_available("-Wshift-negative-value")
add_compiler_flag_if_available("-Wtype-limits")
add_compiler_flag_if_available("-Wunused-but-set-parameter")

if( WIN32 )

    message( STATUS "Configuring Esher-Core on WIN32")

    if ( MINGW )
        message( STATUS "Windows build using MinGW" )
        set( FULL_STATIC_BUILD TRUE )
    else( MINGW )
        set( ZLIB_LIBRARIES "" )
    endif( MINGW )

    SET( DEFAULT_EXECUTABLE_INSTALL_DIR bin/ )

    if( MSVC )
        add_definitions(-DWIN32_LEAN_AND_MEAN)
        #looks like this flag can have different default on some machines.
        SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")

        # Probably cmake has a bug and vcxproj generated for executable in Debug conf. has disabled debug info
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
    endif ( MSVC )

else( WIN32 ) # Apple AND Linux

    if( APPLE )
        # Apple Specific Options Here
        message( STATUS "Configuring Esher-Core on macOS" )
        set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -stdlib=libc++ -Wall -fvisibility-inlines-hidden -fvisibility=hidden" )
    else( APPLE )
        if ( "${CMAKE_SYSTEM_NAME}" STREQUAL "OpenBSD" )
            # OpenBSD Specific Options
            message( STATUS "Configuring Esher-Core on OpenBSD" )
        else()
            # Linux Specific Options Here
            message( STATUS "Configuring Esher-Core on Linux" )
            set( rt_library rt )
        endif()
        # Common Linux & OpenBSD Options
        set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall" )
        if(USE_PROFILER)
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg" )
        endif( USE_PROFILER )
        set( pthread_library pthread)
        if ( NOT DEFINED crypto_library )
          # I'm not sure why this is here, I guess someone has openssl and can't detect it with find_package()?
          # if you have a normal install, you can define crypto_library to the empty string to avoid a build error
          set( crypto_library crypto)
        endif ()
    endif( APPLE )

    if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-memcmp" )
    elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
        if( CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL 4.0.0 OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.0.0 )
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-partial-specialization" )
        endif()
    endif()

    if( "${CMAKE_GENERATOR}" STREQUAL "Ninja" )
        if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics" )
        endif()
    endif()

    # based on http://www.delorie.com/gnu/docs/gdb/gdb_70.html
    # uncomment this line to tell GDB about macros (slows compile times)
    # set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-2 -g3" )

endif( WIN32 )

set(ENABLE_COVERAGE_TESTING FALSE CACHE BOOL "Build Esher-Core for code coverage analysis")

if(ENABLE_COVERAGE_TESTING)
    SET(CMAKE_CXX_FLAGS "--coverage ${CMAKE_CXX_FLAGS}")
endif()

add_subdirectory( libraries )
add_subdirectory( programs )
add_subdirectory( tests )


if (ENABLE_INSTALLER)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)


include(InstallRequiredSystemLibraries)

set(CPACK_OUTPUT_FILE_PREFIX ${CMAKE_BINARY_DIR}/packages)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)

SET(CPACK_PACKAGE_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
set(CPACK_PACKAGE_NAME "graphene")
set(CPACK_PACKAGE_VENDOR "Cryptonomex, Inc.")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION "A client for the Esher-Core network")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A client for the Esher-Core network")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Esher-Core ${CPACK_PACKAGE_VERSION}")

if(WIN32)
 SET(CPACK_GENERATOR "ZIP;NSIS")
 set(CPACK_PACKAGE_NAME "Esher-Core") # override above
 set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
 set(CPACK_NSIS_PACKAGE_NAME "Esher-Core v${CPACK_PACKAGE_VERSION}")
 set(CPACK_NSIS_DISPLAY_NAME "${CPACK_NSIS_PACKAGE_NAME}")
 set(CPACK_NSIS_DEFINES "  !define MUI_STARTMENUPAGE_DEFAULTFOLDER \\\"Esher-Core\\\"")
 # it seems like windows zip files usually don't have a single directory inside them, unix tgz frequently do
 SET(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)

endif(WIN32)

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
endif()

if(LINUX)
  # Linux gets a .tgz
  SET(CPACK_GENERATOR "TGZ")
  SET(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)
endif(LINUX)

 include(CPack)
endif(ENABLE_INSTALLER)

MESSAGE( STATUS "" )
MESSAGE( STATUS "PROFILER: ${USE_PROFILER}" )
MESSAGE( STATUS "" )
