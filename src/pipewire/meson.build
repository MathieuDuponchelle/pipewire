pipewire_headers = [
  'array.h',
  'client.h',
  'command.h',
  'control.h',
  'core.h',
  'data-loop.h',
  'global.h',
  'interfaces.h',
  'introspect.h',
  'link.h',
  'log.h',
  'loop.h',
  'main-loop.h',
  'map.h',
  'mem.h',
  'module.h',
  'node.h',
  'factory.h',
  'pipewire.h',
  'port.h',
  'properties.h',
  'protocol.h',
  'proxy.h',
  'remote.h',
  'resource.h',
  'stream.h',
  'thread-loop.h',
  'type.h',
  'utils.h',
  'work-queue.h',
]

pipewire_sources = [
  'client.c',
  'command.c',
  'control.c',
  'core.c',
  'data-loop.c',
  'global.c',
  'introspect.c',
  'link.c',
  'log.c',
  'loop.c',
  'main-loop.c',
  'mem.c',
  'module.c',
  'node.c',
  'factory.c',
  'pipewire.c',
  'port.c',
  'properties.c',
  'protocol.c',
  'proxy.c',
  'remote.c',
  'resource.c',
  'stream.c',
  'thread-loop.c',
  'type.c',
  'utils.c',
  'work-queue.c',
]

configure_file(input : 'version.h.in',
  output : 'version.h',
  install_dir : join_paths(get_option('includedir'), 'pipewire'),
  configuration : cdata)

install_headers(pipewire_headers, subdir : 'pipewire')

libpipewire_c_args = [
  '-DHAVE_CONFIG_H',
  '-D_GNU_SOURCE',
  '-D_POSIX_C_SOURCE',
]

libpipewire_name = 'pipewire-@0@'.format(apiversion)
libpipewire = shared_library(libpipewire_name, pipewire_sources,
  version : libversion,
  soversion : soversion,
  c_args : libpipewire_c_args,
  include_directories : [pipewire_inc, configinc, spa_inc],
  install : true,
  dependencies : [dl_lib, mathlib, pthread_lib],
)

pipewire_dep = declare_dependency(link_with : libpipewire,
  include_directories : [pipewire_inc, configinc, spa_inc],
  dependencies : [pthread_lib],
)

pkgconfig.generate(filebase : 'lib@0@'.format(libpipewire_name),
  libraries : [libpipewire],
  name : 'libpipewire',
  description : 'PipeWire Interface',
  version : pipewire_version,
  extra_cflags : '-D_REENTRANT',
  variables : ['moduledir=${libdir}/@0@'.format(libpipewire_name)])
