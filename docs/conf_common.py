from esp_docs.conf_docs import *  # noqa: F403,F401

extensions += ['sphinx_copybutton',
               # Needed as a trigger for running doxygen
               'esp_docs.esp_extensions.dummy_build_system',
               'esp_docs.esp_extensions.run_doxygen',
               ]

languages = ['en']
idf_targets = ['esp32c3']

# link roles config
github_repo = 'espressif/esptool'

# context used by sphinx_idf_theme
html_context['github_user'] = 'espressif'
html_context['github_repo'] = 'esptool'

html_static_path = ['../_static']

# Extra options required by sphinx_idf_theme
project_slug = 'privilege_separation'

versions_url = '_static/privilege_separation_versions.js'
