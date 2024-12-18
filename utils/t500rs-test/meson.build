project('t500rs-test', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

gtk_dep = dependency('gtk4')
evdev_dep = dependency('libevdev')
cairo_dep = dependency('cairo')

executable('t500rs-test',
  'main.c',
  'window.c',
  'curve_view.c',
  'device.c',
  dependencies: [gtk_dep, evdev_dep, cairo_dep],
  install : true)
