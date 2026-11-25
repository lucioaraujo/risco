# Risco — avaliação do primeiro núcleo histórico fiel

Atualizado em 2026-11-22. A pesquisa selecionou e incorporou uma base auditada para o primeiro núcleo AY-3-8910/YM2149. O adaptador está ligado ao transporte, pré-escuta, render/exportação, seletor de timbre e projeto V60, mantendo os estudos AY expandidos como opções independentes.

## Estado e critério

`AyToneStudy.hpp`/`AyNoiseStudy.cpp` continuam sendo estudos autorais expandidos, úteis musicalmente, mas não emulação fiel. O núcleo candidato deve oferecer três canais, períodos de tom e ruído, mixer por canal, envelope compartilhado com 16 formas, diferenças AY/YM, níveis não lineares, saída limitada e determinística. A bancada deve ainda medir clock/divisores, escritas no meio do período, período zero, reset/retrigger, aliasing, DC, custo por amostra e estabilidade em 44,1/48/96 kHz.

## Candidatos auditados

| Candidato fixado | Licença observada | Adequação | Decisão |
|---|---|---|---|
| Ayumi, `true-grue/ayumi`, commit `07c08b4874c359169e4a028edf73f046d8b763e2` | MIT, copyright Peter Sovietov | C pequeno; AY/YM separados; três tons, ruído, mixer, envelope compartilhado, tabelas DAC, oversampling/decimação, pan e remoção DC. Não expõe barramento de registradores. No commit auditado, período zero do envelope é convertido em 1, ponto comparado com openMSX. | **Selecionado e incorporado após sandbox A/B**, sem transformar “fiel” em certificação absoluta. LICENSE/copyright e commit estão preservados. |
| MAME `ay8910.cpp`, commit `6f9a2f023e1a951ba34f289d6a4d214a9578676b` | Arquivo declara BSD-3-Clause; MAME completo possui composição própria de licenças | Modelo muito mais detalhado de variantes, registradores, mistura/carga e diferenças AY/YM. Depende da arquitetura MAME e é desproporcional ao instrumento. | **Referência técnica/oráculo secundário**, não dependência do app. Auditar dependências se algum trecho for considerado. |
| Furnace, commit `1f436ae2f98862f97cb98f8f3b294592c1078300` | Aplicação declara GPL-2.0-or-later/GPL-3.0; núcleos embarcados mantêm licenças próprias | Excelente ambiente externo para comparar sequências e múltiplos chips, mas grande demais como núcleo. | **Host externo de comparação**, sem copiar código. |
| libvgm/EMU2149, commit `c5c03a2c2514d38d2ab7b7e8dbd021af0b8292cb` | Proveniência/licença precisa ser fechada por arquivo e cadeia anterior | API de registradores e uso histórico amplo, mas a auditoria consultada não estabeleceu uma licença inequívoca do núcleo isolado. | **Não elegível por enquanto**. |

MIT e BSD-3-Clause são permissivas e podem compor uma distribuição AGPL do Risco desde que avisos e condições sejam preservados. Isso não dispensa a auditoria do pacote realmente copiado nem atribui à documentação ou aos dados medidos a licença do código.

## Bancada fase 0 entregue

`AyRegisterContract.hpp` fixa apenas o domínio de 16 registradores: máscaras, períodos de três tons, ruído, mixer, amplitudes e envelope. `risco_ay_register_contract_check` exercita vetores determinísticos. Não contém tabela DAC, áudio, código ou coeficiente de terceiros e não alega fidelidade. Essa camada permitirá aplicar a mesma agenda a um adaptador candidato sem acoplar a composição ao emulador.

## Sandbox fase 1 entregue

`risco_ayumi_sandbox` foi implementado fora do caminho de áudio do app. Na fase de comparação ele exigia `RISCO_AYUMI_SOURCE_DIR` apontando para um checkout externo auditado. Após a decisão da fase 2B, a mesma revisão foi incorporada em `third_party/ayumi`; o CMake continua conferindo os hashes e permite substituir o caminho apenas por conteúdo byte a byte compatível. O sandbox aceita modelo AY/YM, duração de 0,05–30 s e agenda de até 4096 escritas/1 MiB no formato `segundos registrador valor`. Produz WAV PCM16 estéreo/48 kHz, pico, clipping, FNV-1a do PCM e fator de tempo real. Recusa sobrescrever saída e publica via arquivo parcial.

O checkout temporário validado foi exatamente o commit `07c08b4`; hashes SHA-256 upstream: `ayumi.c` `8824bc343726aa9dc7e4172b14f0e9a0a06ed3f21766277ffb9686b07f19401d`, `ayumi.h` `506e3b75d8269d670d74f75115d3b385ad0f1c232bc797391bd97cc074f17099`, LICENSE `20bb643965c0ddd04e0b74aed83aa00c5ca0afb4805a681a60cbe4827cea66da`. O arquivo de licença no repositório recebeu somente `LF` final e tem hash `e45be409a54a64d7473d15d7d5638959c6c7e6ce90a9f013a7f89351709a3acf`; o texto não mudou.

## Matriz fase 2A entregue

O comando `risco_ayumi_sandbox --matrix` executa 30 casos em AY e YM: tons em períodos 0/1/4095; ruído 0/1/31; todas as 16 formas de envelope; envelope 0/1; retrigger; escrita durante o contador; mixer em tom/ruído/ambos/desligado. São 60 renders mono-agenda/estéreo-saída de um segundo, sem arquivos intermediários. O CSV possui 61 linhas com cabeçalho e colunas `case,model,fnv1a64,peak,clipped,frames`.

Resultado fixado em `/tmp/risco-ayumi-matrix.csv`, SHA-256 `82e1cf28c24ab5c32b14c44bf0375fa1c00fc5a495fedf4b839a95dd317cb3f3`. Nenhum caso clipou. A matriz confirma o comportamento deste commit, não o hardware:

- tom 0 e 1 produzem o mesmo resultado;
- ruído 0 e 1 produzem o mesmo resultado;
- envelope 0 e 1 também produzem o mesmo resultado no Ayumi, hipótese que precisa de confronto externo;
- AY/YM divergem nas tabelas/curvas de envelope, enquanto tom, ruído e volume fixo máximo coincidem;
- mixer desligado ainda produz um transiente associado ao estado DC/filtro inicial; é necessário distinguir comportamento elétrico, política do núcleo e expectativa musical.

O self-check passou a verificar a matriz completa, ausência de clipping, relações 0=1 declaradas e diferença AY/YM no envelope 14. Esses checks detectam mudança do candidato; não certificam exatidão.

## Segunda referência — openMSX 19.1 — fase 2B

Foram baixados do repositório oficial Ubuntu, sem instalação, `openmsx` e `openmsx-data` 19.1+dfsg-1ubuntu3, C-BIOS 0.29a-1 e a biblioteca GLEW necessária. Tudo foi extraído em `/tmp/risco-openmsx-19.1` (21 MiB), executado com vídeo SDL dummy, áudio físico dummy e gravação interna WAV. A máquina C-BIOS MSX2 declara YM2149. O harness autoral `src/tools/openmsx_ay_reference.tcl` espera 100 ms de tempo emulado para o BIOS terminar suas escritas, depois aplica um caso e grava um segundo; cada caso usa processo limpo.

SHA-256 dos pacotes:

- openMSX: `51cdbc7952fb0b74792ed1920b42fb60b777e1d3735d6316a6037a04cfca09d1`;
- openMSX-data: `8063380207c7d0d88a923ebb2e7512283966c8a3ac61f514de544c46e487f87f`;
- C-BIOS: `a640a33e014c0985dae8d54531888476f70eed87f1d765106883917388fdb539`;
- GLEW: `ffef730aac883b1cb1641eae6149da4091a890330d786916f29ea10de362ba9e`.

Os 30 WAVs estão em `/tmp/risco-openmsx-matrix-v4` (2,8 MiB); o arquivo `SHA256SUMS` tem SHA-256 `86aa0ff8debe4f933bc41580346f30b13ae5f703320422d41e1b7d5a53f132da`.

Comparação estrutural:

- tom 0/1 e ruído 0/1 são idênticos bit a bit, concordando com Ayumi;
- as equivalências de forma também coincidem: 0/1/2/3/9 e 4/5/6/7/15;
- retrigger e escrita durante contador alteram o resultado em ambos os núcleos;
- mixer desligado não mantém áudio: após 100 ms, openMSX mede cerca de -90,3 dBFS RMS e Ayumi produz silêncio PCM;
- openMSX 19.1 trata envelope 0 aproximadamente duas vezes mais rápido que 1, enquanto Ayumi usa 0=1.

A última diferença é histórica: o código atual do openMSX afirma 0=1, com medição em YM2149, enquanto openMSX 19.1 e o comentário atual do MAME representam a regra anterior de período zero mais rápido. Não há base para esconder essa diferença sob um único rótulo “fiel”.

## Decisão de integração

O primeiro perfil do Risco seguirá **envelope 0=1**, alinhado a Ayumi e à medição documentada pelo openMSX atual. A compatibilidade antiga não será exposta inicialmente; pode virar perfil explícito somente se repertório, captura de hardware ou formato histórico exigir. Ayumi permanece escolhido para integração por tamanho, licença MIT e concordância estrutural, mas o instrumento manterá separados o núcleo histórico e o estudo AY expandido. “Fiel” descreve o alvo e os limites publicados, não certificação absoluta.

## Integração fase 3A — núcleo e adaptador

`third_party/ayumi` agora preserva `ayumi.c`, `ayumi.h` e `LICENSE` do commit auditado, com proveniência em `README-RISCO.md`. O CMake verifica os três arquivos antes de compilar. `HistoricalAyEngine` é uma camada autoral, stateful e separada do estudo expandido. Ela oferece:

- escolha explícita AY-3-8910 ou YM2149;
- três vozes de tom, com período de 12 bits derivado de frequência e clock;
- níveis fixos de 4 bits usando as curvas DAC do núcleo;
- seleção determinística dos três ataques ativos mais recentes;
- bordas curtas quantizadas para evitar cliques sem criar envelope de hardware fictício;
- reset determinístico quando o tempo volta, adequado a loops;
- saída estéreo centralizada, decimação e remoção DC do Ayumi.

O check dedicado renderiza um acorde com substituição de voz, confirma repetição bit a bit, diferença AY/YM, saída finita/não vazia/limitada e rejeição de configuração inválida. O build Release completo e os cinco testes passaram. Isso valida o contrato de software, não fidelidade absoluta nem qualidade musical.

Limite deliberado: esta primeira camada usa apenas tons/volume fixo. Ruído, mixer combinado, envelope compartilhado e barramento permanecem disponíveis no núcleo, mas só serão expostos depois de o transporte stateful estar correto. O adaptador ainda não está no app nem no formato de projeto.

## Integração fase 3B — instrumento e formato V59

O instrumento agora oferece **AY-3-8910 · histórico** e **YM2149 · histórico**. Reprodução ao vivo, pré-escuta de célula, `SongRender`, exportação mix/stems e CLI offline usam o adaptador stateful. Cada stem possui instância independente; mute/solo não interrompem sua evolução, e fórmulas bytebeat diretas continuam num caminho separado antes dos FX. Seek, reinício e retorno do loop reinicializam o núcleo. O tempo é quantizado à grade da taxa de amostragem antes da seleção de ataques, eliminando diferenças de ponto flutuante entre voltas equivalentes.

V59 aceita timbres 5/6; V58 e anteriores continuam limitados aos modos que conheciam, evitando interpretar um valor novo silenciosamente. O seletor, tooltip, status e Learn em PT/EN/FR/ES explicam a diferença entre estudo expandido e núcleo histórico. A forma de onda arquivada usa render sequencial limitado a 65536 passos, sem esticar ou consultar o motor stateless.

Validação sem janela: build Release completo e CTest 5/5; o teste dedicado compara sequência direta e stem amostra a amostra, exercita `SongRender`, distingue AY/YM, verifica limites e confirma duas voltas idênticas. Depois da correção da grade temporal, passou vinte vezes seguidas. Persistência V59 e rejeição de timbre histórico falsamente rotulado V58 também estão cobertas.

Esse foi o limite da fase 3B. A fase 3C abaixo expõe ruído, mixer tone/noise e
envelope compartilhado com controles históricos próprios, sem reutilizar os
parâmetros do estudo expandido.

## Integração fase 3C — mixer, ruído, envelope e formato V60

O perfil histórico passa a configurar diretamente no Ayumi o mixer de cada
canal (tom, tom + ruído ou ruído), o período compartilhado de ruído e a
envolvente compartilhada. A interface oferece volume fixo, queda única,
subida/sustentação, triângulo e queda repetida, com cinco rampas. Essas opções
são deliberadamente poucas: traduzem gestos musicais para formas do núcleo sem
expor uma grade opaca de registradores. Um ataque novo reinicia a envolvente.

V60 acrescenta o registro `HISTORICAL_AY`; V59 e anteriores recebem defaults
compatíveis. A mesma estrutura chega ao callback ao vivo, pré-escutas,
waveform, `SongRender`, exportação e CLI. O teste dedicado exerce ruído puro,
mistura, envelope, diferenças AY/YM, reset e determinismo; o teste
composicional cobre round-trip V60 e migração V59. Isso amplia o perfil
histórico, mas não certifica precisão de barramento, temporização de escritas
arbitrárias ou equivalência absoluta a uma unidade física.

## Alternativas descartadas nesta etapa

Executar os mesmos casos em MAME ou Furnace e, quando possível, capturas de hardware. As prioridades são período zero do envelope, mixer desligado/DC, retrigger e escrita durante contador. Esses resultados podem justificar um perfil legado ou correções locais documentadas, mas não bloqueiam o adaptador inicial já incorporado.

`emu2149` aparece em vários repositórios, mas sua cadeia histórica/licença do núcleo isolado continua sem declaração inequívoca; não será incorporado. O projeto `wagenheimer/MsxAudio` declara MIT e oferece um port C# recente do emu2149, porém é derivado da mesma linhagem e não funciona como referência independente. openMSX foi usado somente como aplicação externa; seu código GPL e o C-BIOS não entram no Risco.
