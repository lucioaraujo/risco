# Risco — auditoria musical e de uso em 25 nov. 2026

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
| Álbum | As faixas têm duração, BPM e seeds próprias, com projetos editáveis e retomada. A orquestra e o perfil do gerador de álbum ainda partem do mesmo desenho. | Cada faixa precisa ter uma razão musical para diferir da vizinha. Planejar e guardar escolhas por faixa de forma, ritmo, timbre e relações entre vozes. |
| Escuta | A auditoria anterior confirmou integridade e nível em sete WAVs. Os dois mixes gerados mediram −20,5 LUFS e 3,5 LU de LRA. | Comparar peças inteiras, trechos e stems. Medir picos e dinâmica para orientar a escuta e registrar separadamente o que o músico percebe. |

## O que foi aplicado neste lote

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
2. **Orquestração de álbum.** Versionar o plano antes de expandir as receitas
   por faixa. Distribuir ritmos, papéis, timbres e tratamentos em subfluxos
   separados. Comparar duas faixas vizinhas e o percurso do álbum inteiro.
   O músico pode fixar escolhas antes de entregar o restante à seed.
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
