#!/usr/bin/env python
#
# Copyright (C) 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""stack symbolizes native crash dumps."""

import itertools
import logging
import multiprocessing
import os
import re
import struct
import subprocess
import time
import zipfile

import symbol


from pylib import constants

UNKNOWN = '<unknown>'
HEAP = '[heap]'
STACK = '[stack]'
_DEFAULT_JOBS=8
_CHUNK_SIZE = 1000

_BASE_APK = 'base.apk'
_FALLBACK_SO = 'libchrome.so'

_ABI_LINE = re.compile('ABI: \'(?P<abi>[a-z0-9A-Z]+)\'')
_PROCESS_INFO_LINE = re.compile('(pid: [0-9]+, tid: [0-9]+.*)')
# Same as above, but used to extract the pid.
_PROCESS_INFO_PID = re.compile('pid: ([0-9]+)')
_SIGNAL_LINE = re.compile('(signal [0-9]+ \(.*\).*)')
_REGISTER_LINE = re.compile('(([ ]*[0-9a-z]{2} [0-9a-f]{8}){4})')
_THREAD_LINE = re.compile('(.*)(\-\-\- ){15}\-\-\-')
_DALVIK_JNI_THREAD_LINE = re.compile("(\".*\" prio=[0-9]+ tid=[0-9]+ NATIVE.*)")
_DALVIK_NATIVE_THREAD_LINE = re.compile("(\".*\" sysTid=[0-9]+ nice=[0-9]+.*)")
_JAVA_STDERR_LINE = re.compile("([0-9]+)\s+[0-9]+\s+.\s+System.err:\s*(.+)")

_WIDTH = '{8}'

# Matches LOG(FATAL) lines, like the following example:
#   [FATAL:source_file.cc(33)] Check failed: !instances_.empty()
_LOG_FATAL_LINE = re.compile('(\[FATAL\:.*\].*)$')

# Note that both trace and value line matching allow for variable amounts of
# whitespace (e.g. \t). This is because the we want to allow for the stack
# tool to operate on AndroidFeedback provided system logs. AndroidFeedback
# strips out double spaces that are found in tombsone files and logcat output.
#
# Examples of matched trace lines include lines from tombstone files like:
#   #00  pc 001cf42e  /data/data/com.my.project/lib/libmyproject.so
#   #00  pc 001cf42e  /data/data/com.my.project/lib/libmyproject.so (symbol)
# Or lines from AndroidFeedback crash report system logs like:
#   03-25 00:51:05.520 I/DEBUG ( 65): #00 pc 001cf42e /data/data/com.my.project/lib/libmyproject.so
# Please note the spacing differences.
_TRACE_LINE = re.compile('(.*)\#(?P<frame>[0-9]+)[ \t]+(..)[ \t]+(0x)?(?P<address>[0-9a-f]{0,16})[ \t]+(?P<lib>[^\r\n \t]*)(?P<symbol_present> \((?P<symbol_name>.*)\))?')  # pylint: disable-msg=C6310

def InitWidthRelatedLineMatchers():
  global _WIDTH
  global _DEBUG_TRACE_LINE, _VALUE_LINE, _CODE_LINE
  if symbol.ARCH == 'arm64' or symbol.ARCH == 'x86_64' or symbol.ARCH == 'x64':
    _WIDTH = '{16}'
  # Matches lines emitted by src/base/debug/stack_trace_android.cc, like:
  #   #00 0x7324d92d /data/app-lib/org.chromium.native_test-1/libbase.cr.so+0x0006992d
  # This pattern includes the unused named capture groups <symbol_present> and
  # <symbol_name> so that it can interoperate with the |_TRACE_LINE| regex.
  _DEBUG_TRACE_LINE = re.compile(
      '(.*)(?P<frame>\#[0-9]+ 0x[0-9a-f]' + _WIDTH + ') '
      '(?P<lib>[^+]+)\+0x(?P<address>[0-9a-f]' + _WIDTH + ')'
      '(?P<symbol_present>)(?P<symbol_name>)')

  # Examples of matched value lines include:
  #   bea4170c  8018e4e9  /data/data/com.my.project/lib/libmyproject.so
  #   bea4170c  8018e4e9  /data/data/com.my.project/lib/libmyproject.so (symbol)
  #   03-25 00:51:05.530 I/DEBUG ( 65): bea4170c 8018e4e9 /data/data/com.my.project/lib/libmyproject.so
  # Again, note the spacing differences.
  _VALUE_LINE = re.compile('(.*)([0-9a-f]' + _WIDTH + ')[ \t]+([0-9a-f]' + _WIDTH + ')[ \t]+([^\r\n \t]*)( \((.*)\))?')
  # Lines from 'code around' sections of the output will be matched before
  # value lines because otheriwse the 'code around' sections will be confused as
  # value lines.
  #
  # Examples include:
  #   801cf40c ffffc4cc 00b2f2c5 00b2f1c7 00c1e1a8
  #   03-25 00:51:05.530 I/DEBUG ( 65): 801cf40c ffffc4cc 00b2f2c5 00b2f1c7 00c1e1a8
  _CODE_LINE = re.compile('(.*)[ \t]*[a-f0-9]' + _WIDTH + '[ \t]*[a-f0-9]' + _WIDTH +
                          '[ \t]*[a-f0-9]' + _WIDTH + '[ \t]*[a-f0-9]' + _WIDTH +
                          '[ \t]*[a-f0-9]' + _WIDTH + '[ \t]*[ \r\n]')  # pylint: disable-msg=C6310

# This pattern is used to find shared library offset in APK.
# Example:
#    (offset 0x568000)
_SHARED_LIB_OFFSET_IN_APK = re.compile(' \(offset 0x(?P<offset>[0-9a-f]{0,16})\)$')

def PrintTraceLines(trace_lines):
  """Print back trace."""
  maxlen = max(map(lambda tl: len(tl[1]), trace_lines))
  print
  print 'Stack Trace:'
  print '  RELADDR   ' + 'FUNCTION'.ljust(maxlen) + '  FILE:LINE'
  for tl in trace_lines:
    (addr, symbol_with_offset, location) = tl
    normalized = os.path.normpath(location)
    print '  %8s  %s  %s' % (addr, symbol_with_offset.ljust(maxlen), normalized)
  return


def PrintValueLines(value_lines):
  """Print stack data values."""
  maxlen = max(map(lambda tl: len(tl[2]), value_lines))
  print
  print 'Stack Data:'
  print '  ADDR      VALUE     ' + 'FUNCTION'.ljust(maxlen) + '  FILE:LINE'
  for vl in value_lines:
    (addr, value, symbol_with_offset, location) = vl
    print '  %8s  %8s  %s  %s' % (addr, value, symbol_with_offset.ljust(maxlen), location)
  return


def PrintJavaLines(java_lines):
  """Print java stderr lines."""
  print
  print 'Java stderr from crashing pid (may identify underlying Java exception):'
  for l in java_lines:
    if l.startswith('at'):
      print ' ',
    print l

def PrintOutput(trace_lines, value_lines, java_lines, more_info):
  if trace_lines:
    PrintTraceLines(trace_lines)
  if value_lines:
    # TODO(cjhopman): it seems that symbol.SymbolInformation always fails to
    # find information for addresses in value_lines in chrome libraries, and so
    # value_lines have little value to us and merely clutter the output.
    # Since information is sometimes contained in these lines (from system
    # libraries), don't completely disable them.
    if more_info:
      PrintValueLines(value_lines)
  if java_lines:
    PrintJavaLines(java_lines)

def PrintDivider():
  print
  print '-----------------------------------------------------\n'

def ConvertTrace(lines, load_vaddrs, more_info, fallback_monochrome, arch_defined, llvm_symbolizer):
  """Convert strings containing native crash to a stack."""
  InitWidthRelatedLineMatchers()

  if fallback_monochrome:
    global _FALLBACK_SO
    _FALLBACK_SO = 'libmonochrome.so'
  start = time.time()

  chunks = [lines[i: i+_CHUNK_SIZE] for i in xrange(0, len(lines), _CHUNK_SIZE)]
  pool = multiprocessing.Pool(processes=_DEFAULT_JOBS)
  results = pool.map(PreProcessLog(load_vaddrs), chunks)
  useful_log = []
  so_dirs = []
  for result in results:
    useful_log += result[0]
    so_dirs += result[1]
  pool.close()
  pool.join()
  end = time.time()
  logging.debug('Finished processing. Elapsed time: %.4fs', (end - start))
  if so_dirs:
    UpdateLibrarySearchPath(so_dirs)

  # if arch isn't defined in command line, find it from log
  if not arch_defined:
    arch = _FindAbi(useful_log)
    if arch:
      print ('Find ABI:' + arch)
      symbol.ARCH = arch

  ResolveCrashSymbol(list(useful_log), more_info, llvm_symbolizer)
  end = time.time()
  logging.debug('Finished resolving symbols. Elapsed time: %.4fs',
                (end - start))

class PreProcessLog:
  """Closure wrapper, for multiprocessing.Pool.map."""
  def __init__(self, load_vaddrs):
    """Bind load_vaddrs to the PreProcessLog closure.
    Args:
      load_vaddrs: LOAD segment min_vaddrs keyed on mapped executable
    """
    self._load_vaddrs = load_vaddrs;
    # This is mapping from apk's offset to shared libraries.
    self._shared_libraries_mapping = dict()
    # The list of directires in which instead of default output dir,
    # the shared libraries is found.
    self._so_dirs = []

  def _DetectSharedLibrary(self, lib, symbol_present):
    """Detect the possible shared library from the mapping offset of APK
    Return:
       the shared library in APK if only one is found.
    """
    offset_match = _SHARED_LIB_OFFSET_IN_APK.match(symbol_present)
    if not offset_match:
      return
    offset = offset_match.group('offset')
    key = '%s:%s' % (lib, offset)
    if self._shared_libraries_mapping.has_key(key):
      soname = self._shared_libraries_mapping[key]
    else:
      soname, host_so = _FindSharedLibraryFromAPKs(constants.GetOutDirectory(),
                                                  int(offset, 16))
      if soname:
        self._shared_libraries_mapping[key] = soname
        so_dir = os.path.dirname(host_so)
        # Store the directory if it is not the default output dir, so
        # we can update library search path in main process.
        if not os.path.samefile(constants.GetOutDirectory(), so_dir):
          self._so_dirs.append(so_dir)
        print ("Detected: %s is %s which is loaded directly from APK."
                % (host_so, soname))
    return soname

  def _AdjustAddress(self, address, lib):
    """Add the vaddr of the library's first LOAD segment to address.
    Args:
      address: symbol address as a hexadecimal string
      lib: path to loaded library

    Returns:
      address+load_vaddrs[key] if lib ends with /key, otherwise address
    """
    for key, offset in self._load_vaddrs.iteritems():
      if lib.endswith('/' + key):
        # Add offset to address, and return the result as a hexadecimal string
        # with the same number of digits as the original. This allows the
        # caller to make a direct textual substitution.
        return ('%%0%dx' % len(address)) % (int(address, 16) + offset)
    return address

  def __call__(self, lines):
    """Preprocess the strings, only keep the useful ones.
    Args:
      lines: a list of byte strings read from logcat

    Returns:
      A list of unicode strings related to native crash
    """
    useful_log = []
    for ln in lines:
      line = unicode(ln, errors='ignore')
      if (_PROCESS_INFO_LINE.search(line)
          or _SIGNAL_LINE.search(line)
          or _REGISTER_LINE.search(line)
          or _THREAD_LINE.search(line)
          or _DALVIK_JNI_THREAD_LINE.search(line)
          or _DALVIK_NATIVE_THREAD_LINE.search(line)
          or _LOG_FATAL_LINE.search(line)
          or _DEBUG_TRACE_LINE.search(line)
          or _ABI_LINE.search(line)
          or _JAVA_STDERR_LINE.search(line)):
        useful_log.append(line)
        continue

      match = _TRACE_LINE.match(line)
      if match:
        lib, symbol_present = match.group('lib', 'symbol_present')
        if os.path.splitext(lib)[1] == '.apk' and symbol_present:
          soname = self._DetectSharedLibrary(lib, symbol_present)
          if soname:
            line = line.replace('/' + os.path.basename(lib), '/' + soname)
          else:
            # If the trace line suggests a direct load from APK, replace the
            # APK name with _FALLBACK_SO.
            line = line.replace('/' + _BASE_APK, '/' + _FALLBACK_SO)
            logging.debug("Can't detect shared library in APK, fallback to" +
                          " library " + _FALLBACK_SO)
        # For trace lines specifically, the address may need to be adjusted
        # to account for relocation packing. This is because debuggerd on
        # pre-M platforms does not understand non-zero vaddr LOAD segments.
        address, lib = match.group('address', 'lib')
        adjusted_address = self._AdjustAddress(address, lib)
        useful_log.append(line.replace(address, adjusted_address, 1))
        continue

      if _CODE_LINE.match(line):
        # Code lines should be ignored. If this were excluded the 'code around'
        # sections would trigger value_line matches.
        continue
      if _VALUE_LINE.match(line):
        useful_log.append(line)
    return useful_log, self._so_dirs

def ResolveCrashSymbol(lines, more_info, llvm_symbolizer):
  """Convert unicode strings which contains native crash to a stack
  """

  trace_lines = []
  value_lines = []
  last_frame = -1
  pid = -1

  # Collects all java exception lines, keyed by pid for later output during
  # native crash handling.
  java_stderr_by_pid = {}
  for line in lines:
    java_stderr_match = _JAVA_STDERR_LINE.search(line)
    if java_stderr_match:
      pid, msg = java_stderr_match.groups()
      java_stderr_by_pid.setdefault(pid, []).append(msg)

  for line in lines:
    # AndroidFeedback adds zero width spaces into its crash reports. These
    # should be removed or the regular expresssions will fail to match.
    process_header = _PROCESS_INFO_LINE.search(line)
    signal_header = _SIGNAL_LINE.search(line)
    register_header = _REGISTER_LINE.search(line)
    thread_header = _THREAD_LINE.search(line)
    dalvik_jni_thread_header = _DALVIK_JNI_THREAD_LINE.search(line)
    dalvik_native_thread_header = _DALVIK_NATIVE_THREAD_LINE.search(line)
    log_fatal_header = _LOG_FATAL_LINE.search(line)
    if (process_header or signal_header or register_header or thread_header or
        dalvik_jni_thread_header or dalvik_native_thread_header or
        log_fatal_header) :
      if trace_lines or value_lines:
        java_lines = []
        if pid != -1 and pid in java_stderr_by_pid:
          java_lines = java_stderr_by_pid[pid]
        PrintOutput(trace_lines, value_lines, java_lines, more_info)
        PrintDivider()
        trace_lines = []
        value_lines = []
        last_frame = -1
        pid = -1
      if process_header:
        # Track the last reported pid to find java exceptions.
        pid = _PROCESS_INFO_PID.search(process_header.group(1)).group(1)
        print process_header.group(1)
      if signal_header:
        print signal_header.group(1)
      if register_header:
        print register_header.group(1)
      if thread_header:
        print thread_header.group(1)
      if dalvik_jni_thread_header:
        print dalvik_jni_thread_header.group(1)
      if dalvik_native_thread_header:
        print dalvik_native_thread_header.group(1)
      if log_fatal_header:
        print log_fatal_header.group(1)
      continue

    match = _TRACE_LINE.match(line) or _DEBUG_TRACE_LINE.match(line)
    if match:
      frame, code_addr, area, symbol_present, symbol_name = match.group(
          'frame', 'address', 'lib', 'symbol_present', 'symbol_name')
      logging.debug('Found trace line: %s' % line.strip())

      if frame <= last_frame and (trace_lines or value_lines):
        java_lines = []
        if pid != -1 and pid in java_stderr_by_pid:
          java_lines = java_stderr_by_pid[pid]
        PrintOutput(trace_lines, value_lines, java_lines, more_info)
        PrintDivider()
        trace_lines = []
        value_lines = []
        pid = -1
      last_frame = frame

      if area == UNKNOWN or area == HEAP or area == STACK:
        trace_lines.append((code_addr, '', area))
      else:
        logging.debug('Identified lib: %s' % area)
        # If a calls b which further calls c and c is inlined to b, we want to
        # display "a -> b -> c" in the stack trace instead of just "a -> c"
        # To use llvm symbolizer, the hexadecimal address has to start with 0x.
        info = llvm_symbolizer.GetSymbolInformation(
            os.path.join(symbol.SYMBOLS_DIR, symbol.TranslateLibPath(area)),
            '0x' + code_addr)
        logging.debug('symbol information: %s' % info)
        nest_count = len(info) - 1
        for source_symbol, source_location in info:
          if nest_count > 0:
            nest_count = nest_count - 1
            trace_lines.append(('v------>', source_symbol, source_location))
          else:
            trace_lines.append((code_addr,
                                source_symbol,
                                source_location))
    match = _VALUE_LINE.match(line)
    if match:
      (unused_, addr, value, area, symbol_present, symbol_name) = match.groups()
      if area == UNKNOWN or area == HEAP or area == STACK or not area:
        value_lines.append((addr, value, '', area))
      else:
        info = llvm_symbolizer.GetSymbolInformation(
            os.path.join(symbol.SYMBOLS_DIR, symbol.TranslateLibPath(area)),
            '0x' + value)
        source_symbol, source_location = info.pop()

        value_lines.append((addr,
                            value,
                            source_symbol,
                            source_location))

  java_lines = []
  if pid != -1 and pid in java_stderr_by_pid:
    java_lines = java_stderr_by_pid[pid]
  PrintOutput(trace_lines, value_lines, java_lines, more_info)


def UpdateLibrarySearchPath(so_dirs):
  # All dirs in so_dirs must be same, since a dir represents the cpu arch.
  so_dir = set(so_dirs)
  so_dir_len = len(so_dir)
  if so_dir_len > 0:
    if so_dir_len > 1:
      raise Exception("Found different so dirs, they are %s", repr(so_dir))
    else:
      search_path = so_dir.pop()
      print "Search libraries in " + search_path
      symbol.SetSecondaryAbiOutputPath(search_path)


def GetUncompressedSharedLibraryFromAPK(apkname, offset):
  """Check if there is uncompressed shared library at specifc offset of APK."""
  FILE_NAME_LEN_OFFSET = 26
  FILE_NAME_OFFSET = 30
  soname = ""
  sosize = 0
  with zipfile.ZipFile(apkname, 'r') as apk:
    for infoList in apk.infolist():
      filename, file_extension = os.path.splitext(infoList.filename)
      if (file_extension == '.so' and
           infoList.file_size == infoList.compress_size):
        with open(apkname, 'rb') as f:
          f.seek(infoList.header_offset + FILE_NAME_LEN_OFFSET)
          file_name_len = struct.unpack('H', f.read(2))[0]
          extra_field_len = struct.unpack('H', f.read(2))[0]
          file_offset = (infoList.header_offset + FILE_NAME_OFFSET +
                         file_name_len + extra_field_len)
          f.seek(file_offset)
          if offset == file_offset and f.read(4) == "\x7fELF":
            soname = infoList.filename
            sosize = infoList.file_size
            break
  return soname, sosize


def _GetSharedLibraryInHost(soname, dirs):
  """Find a shared library by name in a list of directories.

  Args:
    soname: library name (e.g. libfoo.so)
    dirs: list of directories to look for the corresponding file.
  Returns:
    host library path if found, or None
  """
  for dir in dirs:
    host_so_file = os.path.join(dir, os.path.basename(soname))
    if not os.path.isfile(host_so_file):
      continue
    logging.debug("%s match to the one in APK" % host_so_file)
    return host_so_file


def _FindSharedLibraryFromAPKs(out_dir, offset):
  """Find the shared library at the specifc offset of an APK file.

    WARNING: This function will look at *all* the apks under $out_dir/apks/
    looking for native libraries they may contain at |offset|.

    This is error-prone, since a typical full Chrome build has more than a
    hundred APKs these days, meaning that several APKs might actually match
    the offset.

    The function tries to detect this by looking at the names of the
    extracted libraries. If they are all the same, it will consider that
    as a success, and return its name, even if the APKs embed the same
    library at different offsets!!

    If there are more than one library at offset from the pool of all APKs,
    the function prints an error message and fails.

    TODO(digit): Either find a way to pass a list of valid APKs here, or
    rewrite this script entirely to avoid so many other problematic things
    in it.

  Args:
    out_dir: Chromium output directory.
    offset: APK file offset, as extracted from the stack trace.
  Returns:
    A (library_name, host_library_path) tuple on success, or (None, None)
    in case of failure.
  """
  apk_dir = os.path.join(out_dir, "apks")
  if not os.path.isdir(apk_dir):
    return (None, None)

  apks = [os.path.join(apk_dir, f) for f in os.listdir(apk_dir)
              if (os.path.isfile(os.path.join(apk_dir, f)) and
                   os.path.splitext(f)[1] == '.apk')]
  shared_libraries = []
  for apk in apks:
    soname, sosize = GetUncompressedSharedLibraryFromAPK(apk, offset)
    if soname == "":
      continue
    # The relocation section of libraries in APK is packed, we can't
    # detect library by its size, and have to rely on the order of lib
    # directories; for a specific ARCH, the libraries are in either
    # out_dir or out_dir/android_ARCH, and android_ARCH directory could
    # exists or not, so having android_ARCH directory be in first, we
    # will find the correct lib.
    dirs = [
           os.path.join(out_dir, "android_%s" % symbol.ARCH),
           out_dir,
           ]
    host_so_file = _GetSharedLibraryInHost(soname, dirs)
    if host_so_file:
      shared_libraries += [(soname, host_so_file)]
  # If there are more than one libraries found, it means detecting
  # library failed.
  number_of_library = len(shared_libraries)
  if number_of_library == 1:
    return shared_libraries[0]
  elif number_of_library > 1:
    print "More than one libraries could be loaded from APK."
  return (None, None)


def _FindAbi(lines):
  for line in lines:
    match = _ABI_LINE.search(line)
    if match:
      return match.group('abi')
