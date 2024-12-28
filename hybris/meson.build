project(
  'libhybris-glesv1',
  ['c', 'cpp'],                  # or ['c', 'cpp'] if you had C++ sources
  version : '0.1.0',
  default_options : [
    'warning_level=1',
    'c_std=gnu11'
  ]
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

subdir('common')
subdir('glesv1')