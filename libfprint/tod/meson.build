tod_soversion = 1
tod_version = '@0@.0'.format(tod_soversion)
tod_versioned_path = '-'.join(tod_version.split('.'))
tod_subpath = meson.project_name() / 'tod-@0@'.format(tod_versioned_path)
tod_modules_prefix = get_option('libdir') / tod_subpath

tod_conf = configuration_data()
tod_conf.set_quoted('TOD_DRIVERS_DIR',
    get_option('prefix') / get_option('libdir') / tod_subpath)
configure_file(output: 'tod-config.h', configuration: tod_conf)

gmodule_dep = dependency('gmodule-2.0', version: '>=' + glib_min_version)
deps += gmodule_dep

mapfile = files('libfprint-tod.ver')

libfprint_tod_private = static_library('fprint-tod-private',
    sources: [
        'tod-shared-loader.c',
    ],
    include_directories: include_directories('..'),
    link_with: libfprint_private,
    dependencies: deps,
    install: false,
)

tod_sources = []
foreach source: libfprint_private_sources
    tod_sources += '..' / source
endforeach

libfprint_tod = library('fprint-tod',
    sources: [
        tod_sources,
    ],
    soversion: (soversion * 100) + tod_soversion,
    include_directories: include_directories('..'),
    version: '@0@.'.format(soversion) + tod_version,
    link_args: [
        '-Wl,--version-script,@0@/@1@'.format(meson.source_root(), mapfile[0]),
        '-Wl,--unresolved-symbols=ignore-in-object-files'
    ],
    link_depends: mapfile,
    link_with: [libfprint_private],
    dependencies: deps,
    install: true)

deps += declare_dependency(
    link_with: [
        libfprint_tod,
        libfprint_tod_private,
    ]
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(libfprint_tod,
    name: 'libfprint-tod',
    filebase: 'libfprint@0@-tod-@1@'.format(soversion, tod_versioned_path),
    description: 'Private Libfprint Touch Drivers API',
    version: meson.project_version() + '.@0@'.format(tod_version),
    subdirs: tod_subpath,
    requires_private: [
        meson.project_name() + '@0@'.format(soversion),
    ],
    variables: [
        'tod_driversdir=${libdir}/@0@'.format(tod_subpath)
    ]
)

tod_headers = []
extra_libfprint_headers = [
    'drivers_api.h',
]

drivers_sources += [
    'tod-driver.c',
]

foreach header: libfprint_private_headers + extra_libfprint_headers
    tod_headers += '..' / header
endforeach

custom_target('tod_fpi_enums_headers',
  depends: fpi_enums,
  input: fpi_enums_h,
  output: 'fpi-enums.h',
  command: ['cp', '@INPUT@', '-v', '@OUTPUT@'],
  install: true,
  install_dir: get_option('includedir') / tod_subpath)

install_headers(tod_headers,
    subdir: tod_subpath
)
