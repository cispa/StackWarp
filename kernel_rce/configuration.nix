{ pkgs, lib, ... }:
let

  # kernel-custom = pkgs.linuxKernel.customPackage {
  #   version = "v6.10";
  #   configfile = ../linux/.config;
  #   src = fetchGit ../linux;
  # };

  sshKey = builtins.getEnv "MY_SSH_KEY";
  pwd = builtins.getEnv "PWD";
in
assert (lib.asserts.assertMsg (sshKey != "") "MY_SSH_KEY is not set");
assert (lib.asserts.assertMsg (pwd != "")    "PWD is not set");
{
  users.users.unpriv = {
    isNormalUser = true;
    uid = 1000;
    password = "";
    extraGroups = [ "wheel" "docker" "video" "adbusers" ];
    openssh.authorizedKeys.keys = [
      sshKey
    ];
  };
  services.getty.autologinUser = "unpriv";
  users.users.root.password = "";
  users.mutableUsers = false;

  environment.shellInit = ''
    export PATH="$HOME:$PATH"
  '';

  security.sudo.wheelNeedsPassword = false;

  system.stateVersion = "24.11";

  # boot.kernelPackages = kernel-custom;
  boot.kernelPackages = pkgs.linuxKernel.packagesFor pkgs.linuxKernel.kernels.linux_6_14;

  networking.hostName = "vm";

  virtualisation.vmVariant = {
    virtualisation = {
      memorySize = 4096;
      sharedDirectories = {
        hostShare = {
          source = pwd;
          target = "/home/unpriv";
        };
      };
    };
  };

  console = {
    keyMap = "neo";
  };

  environment.systemPackages = with pkgs; [
    gcc
    gnumake
    file
  ];

  boot.kernelParams = [
    "nokaslr"
    "console=ttyS0"
  ];

  # boot.consoleLogLevel = 5;
  # boot.initrd.verbose = true;
}
