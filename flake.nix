{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        ffmpegWithVapourSynth = pkgs.ffmpeg.overrideAttrs (old: {
          configureFlags = (old.configureFlags or []) ++ [(pkgs.lib.enableFeature true "vapoursynth")];
          nativeBuildInputs = (old.nativeBuildInputs or []) ++ [pkgs.pkg-config];
          buildInputs = (old.buildInputs or []) ++ [pkgs.vapoursynth];
        });
      in {
        devShells = {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              meson
              ninja
              pkg-config
              python312
              uv
            ];

            buildInputs = with pkgs; [
              vapoursynth
            ];

            env = {
              UV_PYTHON_DOWNLOADS = "never";
              UV_PYTHON_PREFERENCE = "only-system";
            };
          };

          ffmpeg = pkgs.mkShell {
            packages = with pkgs; [
              ffmpegWithVapourSynth
              python313
              valgrind
              vapoursynth
            ];

            env = pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
              LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [pkgs.vapoursynth];
            };
          };
        };

        formatter = pkgs.alejandra;
      }
    );
}
