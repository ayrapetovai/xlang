For a Make project, generate the database with Bear:

```sh
bear -- make clean
bear -- make
```

For an incremental project, this is often sufficient:

```sh
bear -- make -B
```

Settings for neovim to use `bear`'s result:

```lua
-- ~/.config/nvim/after/plugin/clangd.lua
vim.lsp.config("clangd", {
  cmd = {
    "clangd",
  -- "--compile-commands-dir=build",
  },
  -- filetypes = { "c", "cpp", "objc", "objcpp" },
  filetypes = { "c", "cpp" },
  root_markers = {
    "compile_commands.json",
    "Makefile",
    -- ".git",
  },
})

vim.lsp.enable("clangd")
```



