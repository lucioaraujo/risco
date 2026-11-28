# Risco — auditoria musical e de uso em 25 nov. 2026, atualizada em 27 nov. 2026

O Risco parte de inscrições, gestos e funções. A ideia de composição é poder
transformar esse material, reconhecê-lo quando volta e intervir no caminho.
O critério para a faixa inteira é uma peça que muda ao longo do tempo, com
espaço para o músico escolher o que conduz essa mudança.

## O teste do autor

O autor procurou uma faixa inteira em **Auto 4b**, ouviu um loop e não teve
confirmação visível quando clicou para renderizar o álbum. Também relatou
dificuldade de encontrar as ações, prever o que vai soar e distinguir
composição de pré-escuta. A leitura gráfica ao vivo e a entrada desenhada
pareceram escolares. A direção visual pedida combina design contemporâneo,
arte retro e pixel art, com espaço para acentos de arte pop.

## O que a geração estava fazendo

| Área | Constatação | Critério para avançar |
|---|---|---|
| Faixa inteira | Dezesseis blocos de quatro compassos com sementes independentes. Os três papéis, o timbre e o mix partiam dos mesmos valores em todas as faixas. | Uma inscrição ou seed alimenta uma forma. Um motivo pode voltar, a orquestra ganha e perde peso e o silêncio pode marcar uma passagem ou o fim. |
| Controle do músico | Os menus reuniam variação por seed, geração de faixa e exportação. O nome **Auto 4b** não esclarecia que o som seria a fórmula em evolução contínua. | Distinguir fonte ouvida, tarefa em curso, receita aplicada e geração que continua ativa. Dar acesso direto à faixa completa. |
| Álbum | As faixas têm duração, BPM e seeds próprias, com projetos editáveis e retomada. Os perfis Original, Parentesco e Contraste alteram forma, pulso e paleta; a fonte do retrato anterior ainda usava apenas um perfil para o álbum inteiro. | Planejar e guardar um perfil por faixa e comparar faixas vizinhas e o percurso completo. A seleção atual cobre forma, pulso e paleta; relações entre stems e tratamentos por faixa continuam como extensão. |
| Escuta | A auditoria anterior confirmou integridade e nível em sete WAVs. Os dois mixes gerados mediram −20,5 LUFS e 3,5 LU de LRA. | Comparar peças inteiras, trechos e stems. Medir picos e dinâmica para orientar a escuta e registrar separadamente o que o músico percebe. |

## O que foi aplicado neste lote

**Nova seed — 27 nov. 2026.** A janela de faixa completa agora oferece uma
ação dedicada para sortear uma variação e gerar uma proposta, inspirada em
**SORTEAR UM PATCH** no app Rasgo Modular. Texto/desenho podem atuar como
origem da variação; seu fingerprint e a seed final são distintos, e a
proveniência preserva a origem. Forma, surpresa, duração, pulso e paleta
compõem a receita. O projeto aberto permanece intacto até o músico revisar,
ouvir e salvar a proposta. Texto/desenho ainda não são analisados por
descritores semânticos ou gestuais nesta geração.

- **Faixa completa…** ao lado do transporte. A fonte Texto ou Desenho pode
  sugerir a seed. O músico escolhe entre **Retorno do tema** e **Colagem e
  rupturas**, com três níveis de surpresa. A proposta informa as escolhas
  e salva um projeto editável. Depois de reaberto, toca uma vez. Loop começa
  desativado.
- O gerador mantém um motivo de abertura e o recupera no último quarto.
  A colagem troca linhagens com mais frequência. Pausas, recuos de vozes e
  efeitos no trecho central criam contraste. O último bloco deixa espaço
  para o fim. Seed, forma e surpresa determinam a peça. A origem V2 é
  preservada nas células. Manifestos de álbum anteriores conservam o
  gerador V1.
- O controle **Fórmula auto** passa a dizer o que faz. Os botões de escuta
  marcam a fonte ativa. A linha sob o gráfico acompanha a ação e informa
  o andamento do render de álbum. As réguas do leitor e do desenho deram
  lugar a marcas discretas em pixel, com os traços e o cursor em primeiro
  plano.
- O modo Álbum aceita uma sequência opcional de perfis por faixa, iniciada na
  faixa 1: `O` (Original), `R` (Parentesco), `C` (Contraste) e `-` (usar o
  perfil geral). A revisão mostra o perfil efetivo junto das escolhas de forma,
  pulso, paleta e surpresa. O manifesto V3 registra as sobrescritas; o código
  mantém caminhos de leitura para V1 e V2. Em 26 nov., o alvo Release do app
  compilou e os testes direcionados de plano e manifesto passaram 2/2. A
  cobertura inclui V1/V2/V3, round-trip de sobrescritas, checkpoint retomável
  e rejeição de perfil alterado no cabeçalho V3. A interface ainda não foi
  aberta para inspeção visual.

## Nota de escuta em sessão — 26 nov. 2026

Ao observar a faixa completa e o fluxo de Salvar WAV para o Álbum, o autor
perguntou por canais independentes para ritmo e textura. Em 26 nov. autorizou a
implementação de dois canais além dos três papéis existentes: **Ritmo** para
ataques curtos e **Textura** para camadas sustentadas. O seletor de destino da
célula também deve ligar a entrada escrita/desenhada à linha escolhida.

O lote em andamento amplia o arranjo a cinco stems, gera material de ritmo e
textura na faixa completa, mantém controles e exportação por stem e aumenta o
banco para comportar as faixas de 64 compassos. Projetos `.risco` antigos
continuam lendo seus três stems; os dados dos novos stems ficam num registro
opcional ao fim do arquivo. Ainda falta compilar e validar a persistência, a
reprodução, o WAV individual e a revisão visual/auditiva.

## Par para escuta, mesma seed

Build Release e CTest 12/12 passaram. O teste de geração compara dois
projetos idênticos com a mesma receita, verifica o retorno do motivo e
confere a duração musical de 64 compassos nas duas formas.

Os dois WAVs foram renderizados em sequência, em PCM24 estéreo a 48 kHz.
O pico de amostra ficou abaixo de 0 dBFS em ambos.

| Forma | Surpresa | Duração WAV | LUFS I | LRA | Pico verdadeiro |
|---|---|---:|---:|---:|---:|
| Retorno do tema | Média | 137,665 s | −20,9 | 3,9 LU | −9,4 dBTP |
| Colagem e rupturas | Alta | 137,665 s | −21,1 | 4,3 LU | −8,2 dBTP |

Os projetos, WAVs e logs estão em
`.local-build/auditoria-musical.oko12G/`. O relatório do autor sobre a
escuta e a revisão visual da interface ainda estão pendentes.

## Próximos critérios de aceite

1. **Escuta antes de gravar.** A revisão oferece **Ouvir prévia** e **Parar
   prévia** com a mixagem da proposta. A escuta dispensa gravar um projeto e
   o transporte anterior volta ao fechar a revisão. Falta comparar as duas
   formas com a mesma seed e registrar onde se ouve retorno, pausa ou mudança
   de voz.
2. **Orquestração de álbum.** O plano V3 e os perfis por faixa estão
   implementados; os testes direcionados de plano e manifesto passaram 2/2,
   incluindo round-trip V1/V2/V3. Falta comparar duas faixas vizinhas e o
   percurso do álbum inteiro. Papéis dos stems, relações entre vozes e
   tratamentos ainda não têm sobrescritas próprias por faixa.
3. **Material autoral.** Ampliar como a inscrição orienta ritmo, densidade,
   relação entre vozes e forma. Preservar a origem e permitir retomar uma
   versão antes da transformação.
4. **Uso real.** Observar um primeiro percurso de tocar, gerar, salvar e
   renderizar. Confirmar qual fonte soa e se a ação foi concluída, cancelada
   ou bloqueada. Registrar também o que permanece difícil de encontrar.

A primeira linha de forma e memória vem da [arquitetura do Risco](ARQUITETURA_E_FORMAS_DE_COMPOSICAO.md).
A linhagem, o corte audível e o retorno dialogam com o
[eixo de colagem do RASGO](../../RASGO_DOCUMENTATION/GOVERNANCA_E_TRANSVERSALIDADE.md#82-eixo-de-colagem-corte-e-acontecimento).
O método de medir e comparar faixas vem da
[auditoria de Planches](../RASGO_SYNTH_STUDIO/docs/ANALISE_ALBUM_PLANCHES_2026-10-10.md).
Separar sorteios por domínio e opções do músico segue a
[auditoria de seed do Asterismus](../RASGO_SYNTH_ASTERISMUS/docs/AUDITORIA_SEED_COMPOSICAO_2026-11-05.md).

## Interação generativa — 27 nov. 2026

Texto e desenho passam a alimentar a escuta enquanto são editados. Alterações
de texto e pontos do gesto atualizam o material sem reiniciar a posição do
transporte; o transporte de entrada inicia em loop automaticamente. Texto usa
a seed do conteúdo para escolher uma das paletas harmônicas do Risco, formar
melodia e acrescentar baixo/acordes diatônicos em início de frase. O modo
original da leitura preserva os papéis de baixo e harmonia, necessários à
reprodução da cena polifônica.

O botão **AUTO**, ativo por padrão no banco/grade, deriva o papel musical da
célula a partir de registro, densidade e atividade. Ao inserir, a sugestão
avalia o espaço da grade e a seed para escolher início e stem; clicar num stem
ou desligar AUTO mantém a escolha manual. A coleta periódica remove snapshots
de áudio antigos fora do callback e preserva o snapshot atualmente tocado.

O alvo `risco_app` Release compilou depois dessas alterações. `git diff
--check` passou. Não rodei testes nem fiz escuta ou inspeção visual; a
qualidade final da mixagem generativa, o gesto ao vivo e a distribuição AUTO
precisam de avaliação autoral. Próximo passo: tocar textos curtos/longos e
desenhos lentos/rápidos; experimentar AUTO e manual, conferir se as decisões
de papel/canal resultam em entradas musicais interessantes e ajustar níveis
ou regras a partir da escuta.

## Derivação ao vivo e gesto — 27 nov. 2026

AUTO agora evolui a entrada a cada volta do loop: reflexão e inversão de alturas,
deslocamento de ataques, retrogradação, rearmonização determinística, glitch
suave e inserção de eventos novos. Seed + número de volta reproduzem cada
derivação. AUTO agora também compõe uma camada rítmica curta, com acentos e
microdesvios determinísticos derivados dos ataques, papéis e intensidades da
inscrição, além de uma textura harmônica sustentada e esparsa.
Melodia, baixo, harmonia, ritmo e textura são encaminhados aos cinco papéis
correspondentes da mixagem, cada qual com volume, pan, mute/solo e FX próprios,
inclusive como camada sobre a música da grade já em execução. Os seletores de
papel ficam visíveis ao lado das cinco pistas da grade. Ao desligar AUTO, a leitura e a derivação ficam sob
controle manual e todo o gesto segue o stem escolhido. Ouvir texto/desenho usa
esse mesmo caminho.

O botão AUTO ganhou dois níveis de interação: clique esquerdo liga ou desliga
a geração; clique direito abre **Força da evolução** (suave 40%, equilibrada
70%, intensa 100%) e **Frequência** (a cada 1, 2 ou 4 voltas). A força mistura
gradualmente cada transformação com a inscrição original, preservando a
identidade do gesto nos níveis mais baixos; eventos novos também têm o ganho
reduzido pela força. O padrão segue equilibrado e transforma a cada volta.

Com AUTO ativo, **Inserir célula** usa a seed, o início indicado, o papel e a
ocupação da grade para escolher tempo e canal. Clicar numa área vazia também
insere com AUTO, usando o clique como referência temporal, sem forçar o canal
clicado. Com AUTO desligado, o clique mantém a escolha manual do tempo e STEM.
Se a busca não encontrar entrada válida,
não insere em silêncio no destino manual: informa o problema e permite ajustar
o tempo ou desligar AUTO. A avaliação de densidade usa uma tabela de perfis com
a mesma capacidade do banco (128 células), evitando ultrapassar o limite quando
a música tiver mais de 64 células.

No desenho, posição horizontal desloca entradas e contorno; posição vertical
orienta altura e intensidade. Dispositivos com pressão disponível também mudam
a intensidade e a espessura visual do traço. Alt+arraste desloca um traço e
recalcula o som; X silencia/reabilita o último traço. Desfazer/Refazer percorre
até 32 edições de traço, deslocamento e silêncio. Uma edição nova limpa o
histórico de refazer. Pressão é persistida no formato `.risco` V77; projetos V71–V76
continuam lendo com pressão inferida pela posição vertical. O editor suaviza a
pressão em uma vizinhança curta antes de convertê-la em dinâmica e espessura,
reduzindo saltos entre amostras. Quando o traço alcança 128 pontos, remove o
ponto menos informativo segundo o desvio da curva e da pressão, preservando os
extremos; não altera o formato V77.

AUTO também varia entre posições com pontuação até 12 pontos acima da melhor,
usando a seed da célula e a quantidade de inserções para selecionar de modo
reproduzível. A sugestão manual, sem seed generativa, continua retornando o
menor score exato. O conjunto candidato é fixo e limitado a 17 compassos × 5
canais; nenhuma alocação dinâmica foi acrescentada.

O Release recompilou com a pontuação AUTO atualizada. `git diff --check` passou.
Binário de 27 nov. 2026, 19:02 CET, SHA-256
`946f02c9424435fa2b272aea46235c8f06571d2a84c54f4e53ee706f5a06b203`. Não rodei
testes nem escutei o resultado. A janela Risco encerrada pelo pedido do autor
mostrava três linhas; ainda falta confirmar visualmente as cinco linhas e ouvir
se as posições próximas trazem variedade musical sem perder coerência. Próximo
passo: avaliar a interface e a escuta quando o autor retomar a sessão interativa.
