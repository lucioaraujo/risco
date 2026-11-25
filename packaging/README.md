# Risco para Linux

Em Debian ou Ubuntu compatível com as dependências do pacote, instale com
`sudo apt install ./risco-0.1.0-linux-x86_64.deb`. Abra **Risco** pelo menu de
aplicativos ou com o comando `Risco`. O instrumento usa ALSA e uma sessão
gráfica que ofereça X11 (diretamente ou via XWayland).

Para desinstalar, use `sudo apt remove risco`. As músicas e configurações do
usuário ficam nas pastas em que o aplicativo as gravou.

## Compilar e empacotar

Use CMake 3.22 ou posterior, um compilador C++17 e as ferramentas de build
do Debian. A instalação de build precisa dos headers de áudio ALSA, X11,
fontconfig, FreeType e GL. A árvore JUCE deve corresponder à versão 9.0.3.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRISCO_PACKAGE_LINUX=ON \
  -DRISCO_JUCE_SOURCE_DIR="$PWD/third_party/JUCE-9.0.3"
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -B build/packages
```

O caminho acima corresponde ao arquivo de fontes que acompanha o pacote.
No workspace do RASGO, o CMake encontra a árvore compartilhada do JUCE se
`RISCO_JUCE_SOURCE_DIR` for omitido.

Licenças, créditos e notas da versão ficam em `/usr/share/doc/risco`. Os
projetos `.risco` salvos por esta versão usam o formato V76. Para relatar um
problema, envie a versão do Risco, sistema operacional e passos para
reproduzir a rasgo.instruments@gmail.com.
