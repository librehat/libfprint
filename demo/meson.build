gtk_test_resources = gnome.compile_resources('gtk-test-resources', 'gtk-libfprint-test.gresource.xml',
                                             source_dir : '.',
                                             c_name : 'gtk_test')

executable('gtk-libfprint-test',
           [ 'gtk-libfprint-test.c', 'loop.c', 'loop.h', gtk_test_resources ],
           dependencies: [ libfprint_dep, gtk_dep ],
           include_directories: [
             root_inc,
           ],
           c_args: [ common_cflags,
                     '-DPACKAGE_VERSION="' + meson.project_version() + '"' ]
           )
