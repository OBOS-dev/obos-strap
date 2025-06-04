# Recipe format
Examples can be found under [tests/recipes](tests/recipes)
## Valid JSON fields in a package recipe:
#### name: string (required)
- The name of the package.<br/>
#### description: string (optional)
- A light description of the package.<br/>
#### git-url: string
- The URL of the git repo to clone<br/>
#### git-commit: string (required if git-url is present)
- The commit to checkout<br/>
#### url: string
- The URL to an archive. This archive must be of format '.tar.*'.<br/>
#### depends: string array (required)
- Dependencies of the project. These will be build before the requested package is built.<br/>
#### patches: patch array (optional)
- An array of patches that should be applied before bootstrap commands are run.<br/>
- A patch is defined as follows:
```json
"patches": [
    {
        "modifies": "path/to/file",
        "patch": "path/to/patch",
	"delete-file": 0,
    }
]
```
- The "modifies" field is relative to ${repo_directory}
- The "patch" field is relative to the directory obos-strap was run in.
- The "delete-file" field specifies whether to delete $modifies before patching, can be either zero or one. Optional field. (default: 0)
- Patches should be generated as if they were generated with `diff -u FILE1 FILE2`
#### bootstrap-commands: array of string arrays (required)
- Commands run to "bootstrap" the build. These commands will be run under ${bootstrap_directory}/${name}/<br/>
#### build-commands: array of string arrays (required)
- Commands run to build the package. These commands will be run under ${bootstrap_directory}/${name}/<br/>
#### install-commands: array of string arrays (required)
- Commands run to install the package into ${prefix_directory}. These commands will be run under ${bootstrap_directory}/${name}/<br/>
#### run-commands: array of string arrays (optional)
- Commands run when obos-strap run is executed on the package. These commands will be run in the directory obos-strap was run in.<br/>
#### host-package: boolean (optional, defaults to false)
- Whether this package is a host package or target package. Ignored if not cross compiling.
## Valid substitution strings in commands:
```
bootstrap_directory: Output of bootstrap commands goes here, as well as built binaries
               name: The name field of the package.
        description: The description field of the package.
     repo_directory: Cloned repositories and decompressed archives.
             prefix: The prefix directory. This can change depending on whether this is a host package or not.
        host_prefix: The host prefix directory.
      target_prefix: The target prefix directory.
              nproc: The CPU count of the system.
     target_triplet: The target triplet.
```
- To access these, do ${insert_name_here}
- To access an environment variable, do $ENV
