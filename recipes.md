# Recipe format
Examples can be found under tests/recipes/
## Valid JSON fields in a package recipe:
name: string (required)
- The name of the package.
description: string (optional)
- A light description of the package.
git-url: string ((required if url is non-present)
- The URL of the git repo to clone
git-commit: string (required if git-url is present)
- The commit to checkout
url: string (required if git-url is non-present)
- A URL to a .tar.* archive, .zip archive, or .7z archive.
depends; string array (required)
- Dependencies of the project. These will be build before the requested package is built.
patches: path array (optional, ignored if git-url is not present)
- An array of patches that should be applied before bootstrap commands are run.
bootstrap-commands: array of string arrays (required)
- Commands run to "bootstrap" the build. These commands will be run under ${bootstrap_directory}/${name}/
build-commands: array of string arrays (required)
- Commands run to build the package. These commands will be run under ${bootstrap_directory}/${name}/
install-commands: array of string arrays (required)
- Commands run to install the package into ${prefix_directory}. These commands will be run under ${bootstrap_directory}/${name}/
## Valid substitution strings in commands:
```
bootstrap_directory: Output of bootstrap commands goes here, as well as built binaries
               name: The name field of the package.
     repo_directory: Cloned repositories and decompressed archives.
             prefix: The prefix directory.
```
- To access these, do ${insert_name_here}
- To access an environment variable, do $ENV
