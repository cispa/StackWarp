{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, ... }:
    let
      system = "x86_64-linux";
    in
    {
      nixosConfigurations.vm = nixpkgs.lib.nixosSystem {
        inherit system;
        modules = [
          ./configuration.nix
        ];
      };
    };
}
