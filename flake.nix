{
  description = "QuickJS (Nova fork)";

  inputs = {
    nixpkgs = {
      url = "github:nixos/nixpkgs?rev=067d5d5b89133efcda060bba31f9941c6396e3ee";
    };

    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "x86_64-darwin" "aarch64-darwin" ] (system:
      let
        pkgs = import nixpkgs
          {
            inherit system;
            overlays = [
              (self: super: {
                quickjs = super.quickjs.overrideAttrs (old: {
                  # LTO support must be disabled on macos
                  buildFlags = if (system == "aarch64-darwin" || system == "x86_64-darwin") then [ "CONFIG_LTO=" ] else (old.buildFlags or [ ]);
                  installCheckPhase = "";
                  src = ./.;
                })
                ;
              })
            ];
          };
      in
      {
        packages.quickjs = pkgs.quickjs;

        defaultPackage = self.packages.${system}.quickjs;

        devShell = pkgs.mkShell {
          name = "quickjs";

          buildInputs = [
            self.packages.${system}.quickjs
          ];

          shellHook = ''
            echo "To compile a JS file, use qjsc -o <binary> <source>" 1>&2
            echo "To run a JS file, use qjs <source>" 1>&2
          '';
        };
      });
}
