# port_adaptor
#
#     Copyright 2010 NorthScale, Inc.
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
AC_PREREQ(2.59)
m4_include([m4/version.m4])
AC_INIT(port_adaptor, VERSION_NUMBER, trond.norbye@gmail.com)
AC_CONFIG_SRCDIR([src/unix_adaptor.c])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

PANDORA_CANONICAL_TARGET(less-warnings, warnings-always-on)

AC_CHECK_HEADERS_ONCE([windows.h])
AM_CONDITIONAL(BUILD_UNIX_ADAPTOR, 
               [test "x${ac_cv_header_windows_h}" = "xno"])

dnl ----------------------------------------------------------------------------

AC_CONFIG_FILES(Makefile)
AC_OUTPUT
echo "---"
echo "Configuration summary for $PACKAGE_NAME version $VERSION"
echo ""
echo "   * Installation prefix:       $prefix"
echo "   * System type:               $host_vendor-$host_os"
echo "   * Host CPU:                  $host_cpu"
echo "   * C Compiler:                $CC_VERSION"
echo "   * C++ Compiler:              $CXX_VERSION"
echo "   * Assertions enabled:        $ac_cv_assert"
echo "   * Debug enabled:             $with_debug"
echo "   * Warnings as failure:       $ac_cv_warnings_as_errors"
echo ""
echo "---"
