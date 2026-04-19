{
  description = "Unofficial linux driver for the Sennheiser BTD 700 dongle";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" "riscv64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
    packages = forAllSystems (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
        {
        default = pkgs.stdenv.mkDerivation {
          pname = "btd700ctl";
          version = "unstable-${self.shortRev or "dirty"}";

          src = ./.;

          nativeBuildInputs = with pkgs; [ cmake pkg-config makeWrapper ];
          buildInputs = with pkgs; [ hidapi ];

          postInstall = ''
              install -Dm644 ../udev/99-btd700.rules -t $out/lib/udev/rules.d/
              substituteInPlace $out/lib/systemd/user/btd700d.service \
              --replace-fail "/usr/local/bin/btd700d" "$out/bin/btd700d"
              wrapProgram $out/bin/btd700d \
              --prefix PATH : ${pkgs.wireplumber}/bin
              '';

          meta = with pkgs.lib; {
            description = "Unofficial linux driver for the Sennheiser BTD 700 dongle";
            homepage = "https://github.com/sobalap/btd700ctl";
            license = licenses.lgpl21Only;
            platforms = platforms.linux;
            mainProgram = "btd700ctl";
          };
        };
      });
    nixosModules.default = { pkgs, lib, config, ... }: 
      let
        pkg = self.packages.${pkgs.system}.default;
      in {
      assertions = [
        {
          assertion = config.services.pipewire.enable == true;
          message = "The btd700d module requires 'services.pipewire.enable = true;'";
        }
        {
          assertion = config.services.pipewire.wireplumber.enable == true;
          message = "The btd700d module requires 'services.pipewire.wireplumber.enable = true;'";
        }
      ];

      services.udev.packages = [ pkg ];
      environment.systemPackages = [ pkg ];
      systemd.packages = [ pkg ];

      systemd.user.services.btd700d = {
        wantedBy = [ "default.target" ];
        enable = true;
      };
    };
  };
}
