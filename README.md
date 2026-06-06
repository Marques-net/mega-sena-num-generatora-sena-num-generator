# Mega-Sena Number Generator

Simulador C++ de um globo de sorteio da Mega-Sena usando um modelo DEM
simplificado para 60 bolas numeradas.

O objetivo e simular mistura e extracao de bolas por dinamica fisica aproximada.
O projeto nao tenta prever resultados reais, nem deve ser usado como modelo
estatistico oficial de sorteios.

## Observacao do video

O video `sorteio-mega.webm` mostra uma camara superior circular/transparente,
com bolas misturadas por fluxo de ar. A extracao visivel acontece por uma
portinhola/gargalo central inferior, que leva a bola para uma rampa/compartimento
transparente abaixo. Por isso, a especificacao original foi ajustada em um ponto:

- a zona de captura padrao e `outlet`, posicionada no gargalo central inferior;
- tambem existe `--capture-mode top-side` caso se queira testar a captura
  topo/lateral descrita inicialmente.

As dimensoes continuam simplificadas em 3D:

- largura: `0.50 m`
- altura: `0.60 m`
- profundidade: `0.25 m`
- raio da bola: `0.025 m`
- massa da bola: `0.06 kg`

## Modelo fisico

Cada bola tem `id`, posicao, velocidade, aceleracao, massa, raio, coeficiente de
restituicao, atrito e cor por faixa de dezenas.

O solver usa integracao explicita:

```text
a(t) = F(t) / m
v(t + dt) = v(t) + a(t) dt
x(t + dt) = x(t) + v(t + dt) dt
```

Forcas externas:

```text
Fg = m g
F_drag = -0.5 rho Cd A |v| v
F_air = F_drag + F_jet + F_turbulence
```

O jato ascendente e mais forte perto da base:

```text
F_jet = upward_strength * base_profile(y) * radial_profile(x,z) * noise * direction
```

A turbulencia e um campo pseudoaleatorio suavizado no tempo, com seed fixa para
reprodutibilidade.

Contato bola-bola:

```text
delta = xj - xi
n = normalize(delta)
penetration = ri + rj - |delta|
Fn = kn * penetration - gammaN * dot(vj - vi, n)
```

Uma friccao tangencial limitada por Coulomb e aplicada de forma simplificada.
Colisoes com parede usam correcao geometrica e reflexao da componente normal da
velocidade com restituicao e atrito tangencial.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Execucao

```bash
./build/mega_sena_num_generator --seed 12345 --output-dir output
```

Arquivos gerados:

- `output/trajectory.csv`
- `output/result.txt`

Exemplo de execucao mais curta para teste:

```bash
./build/mega_sena_num_generator \
  --seed 42 \
  --max-time 30 \
  --min-mix-time 3 \
  --csv-interval 0.05 \
  --output-dir output-test
```

## Opcoes

```text
--seed <n>              Seed aleatoria
--output-dir <path>     Diretorio de saida
--dt <seconds>          Timestep interno
--max-time <seconds>    Duracao maxima simulada
--min-mix-time <sec>    Tempo minimo antes da captura
--csv-interval <sec>    Intervalo de amostragem do CSV
--jet <newtons>         Intensidade do jato ascendente
--turbulence <newtons>  Intensidade da turbulencia
--capture-mode <mode>   outlet ou top-side
```

## Criterios atendidos

- C++17 com CMake.
- Codigo modular nas classes solicitadas:
  `Vec3`, `Ball`, `SimulationConfig`, `LotteryMachine`, `DEMSolver`,
  `RandomField`, `CaptureZone`, `CsvWriter`.
- DEM simplificado com gravidade, arrasto, jato, turbulencia, colisao
  bola-bola e bola-parede.
- Seed reprodutivel.
- Sem forcas dependentes do numero da bola.
- IDs sao embaralhados na inicializacao para reduzir vies de posicao inicial.
- Saida em CSV para analise.
- `result.txt` com ordem de extracao e dezenas ordenadas.

## Nota para Monte Carlo

O binario executa uma simulacao por vez. Para milhares de rodadas, use seeds
diferentes e direcione cada saida para um diretorio distinto. O CSV e amostrado
por `--csv-interval`, separado do timestep interno, para reduzir volume de dados.

