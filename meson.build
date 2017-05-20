project('randio', 'c',
    default_options : ['c_std=gnu11' ],
    license: 'GPL3+'
  )

gnome = import('gnome')
c_compiler = meson.get_compiler('c')

# Add additional warnings if available
extrawarnings = ['-Wall', '-Wshadow', '-Wwrite-strings', '-Wstrict-prototypes', '-Wmissing-prototypes', '-Wmissing-declarations','-Wold-style-declaration','-Wold-style-definition']
foreach w : extrawarnings
  if c_compiler.has_argument(w)
    add_project_arguments(w, language: 'c')
  endif
endforeach

add_project_arguments('-Wl,--export-dynamic',language: 'c')

# Check for required headers
requiredHeaders = ['string.h','time.h','dirent.h','stdbool.h','sys/types.h','stdlib.h']
foreach h : requiredHeaders
    if not c_compiler.has_header(h)
        error('Header @0@ was not found, but is required'.format(h))
    endif
endforeach

# All of our dependencies
randioDeps = [
  dependency('gtk+-3.0'),
  dependency('gstreamer-1.0'),
  dependency('sqlite3'),
  dependency('rest-0.7'),
  dependency('rest-extras-0.7'),
  # Needed for signal handlers in the gtkbuilder definitions
  dependency('gmodule-export-2.0')
  ]

# Build GResources
resources = gnome.compile_resources ('randio-resources',
    'src/randio.gresource.xml',
    source_dir: 'src/',
    c_name: 'randio')

# Build randio
executable('randio', [ 'src/randio.c', 'src/randio-sql.c', 'src/randio-lastfm.c', 'src/randio-prefs.c' ] + resources, dependencies: randioDeps, install: true)
