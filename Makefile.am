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

ACLOCAL_AMFLAGS = -I m4 --force

bin_PROGRAMS = port_adaptor

if BUILD_UNIX_ADAPTOR
port_adaptor_SOURCES = src/unix_adaptor.c
else
port_adaptor_SOURCES = src/windows_adaptor.c
endif

check_PROGRAMS=
TESTS=${check_PROGRAMS}

test: check-TESTS
