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

## API HTTP

O mesmo binario pode operar como servico HTTP para integracao com o GameHub:

```bash
./build/mega_sena_num_generator --serve --port 8080 --output-dir /tmp/mega-sena-runs
```

Endpoints:

- `GET /health/live`
- `GET /health/ready`
- `POST /api/mega-sena-simulator/simulate`

Exemplo:

```bash
curl -s http://127.0.0.1:8080/api/mega-sena-simulator/simulate \
  -H 'Content-Type: application/json' \
  -d '{"seed":42,"maxTime":30,"minMixTime":3,"captureMode":"outlet"}'
```

A resposta retorna a ordem de extracao, dezenas ordenadas, parametros efetivos
e uma amostra da trajetoria para visualizacao no front.

## Container

Build no cluster MK:

```bash
../talos-cluster/local-cluster-mk/scripts/build-mega-sena-simulator-image.sh
```

Imagem publicada:

```text
ghcr.io/marques-net/mega-sena-num-generator:<tag>
```

```bash
docker build -t mega-sena-num-generator .
docker run --rm -p 8080:8080 mega-sena-num-generator
```

## Agente de calibracao historica

O binario `mega_sena_calibration_agent` executa uma busca retrospectiva de
parametros do DEM contra resultados historicos da Mega-Sena armazenados em
MongoDB. Ele usa os componentes do simulador (`LotteryMachine`, `DEMSolver`,
`SimulationConfig`) e calibra os parametros em ciclos ate encontrar uma
correspondencia.

A calibracao atual usa um mecanismo online de backpropagation sobre um modelo
substituto pequeno. A cada tentativa, o agente observa a saida do simulador,
treina esse modelo substituto com backpropagation e calcula o gradiente do erro
contra as dezenas alvo. Esse gradiente ajusta os parametros fisicos da proxima
tentativa, mantendo uma componente de exploracao por seed para evitar ficar preso
em um unico ponto do espaco de busca.

A imagem do projeto tambem empacota o agente e o `mongosh`, necessario para ler
os resultados historicos e persistir checkpoints no MongoDB.

Parametros ajustados pelo ciclo:

- seed `uint64`;
- intensidade do jato de ar;
- intensidade da turbulencia;
- coeficiente de restituicao;
- coeficiente de atrito;
- tempo minimo de mistura;
- raio e velocidade maxima da zona de captura;
- modo de captura `outlet` ou `top-side`;
- amortecimento normal e tangencial.

Por padrao, o agente busca os 10 ultimos concursos em
`geek_hub.mega_sena_resultados` e grava progresso/resultado em
`geek_hub.mega_sena_simulacoes` com `documentType` igual a
`dem_parameter_calibration`.

Exemplo de execucao continua:

```bash
export MONGO_URI='mongodb://root:<senha>@127.0.0.1:27017/geek_hub?authSource=admin'

mega_sena_calibration_agent \
  --latest 10 \
  --database geek_hub \
  --history-collection mega_sena_resultados \
  --calibration-collection mega_sena_simulacoes \
  --output-dir calibration-output
```

Execucao paralela por shards:

```bash
mega_sena_calibration_agent \
  --latest 10 \
  --database geek_hub \
  --history-collection mega_sena_resultados \
  --calibration-collection mega_sena_simulacoes \
  --worker-index 0 \
  --worker-count 4 \
  --checkpoint-every 100 \
  --sync-every 100
```

Em Kubernetes, use um Job `Indexed` e passe `JOB_COMPLETION_INDEX` como
`--worker-index`. Cada pod processa apenas as tentativas do seu shard e consulta
periodicamente o MongoDB para parar quando outro worker encontrar o match.

O agente nao define limite de tentativas por padrao. Ele continua em cada
concurso ate encontrar uma simulacao cujas 6 dezenas ordenadas sejam iguais ao
resultado historico. Para validar sem entrar em busca longa:

```bash
mega_sena_calibration_agent \
  --latest 10 \
  --dry-run \
  --max-attempts-per-contest 1 \
  --max-time 0.2 \
  --min-mix-time 0.05 \
  --dt 0.002
```

Campos principais gravados no MongoDB:

- `agentName`, `agentVersion` e `documentType`;
- `status`: `running`, `attempt_limit` ou `matched`;
- `concurso`, `dataSorteio`, `targetNumbers` e `targetOrder`;
- `attempts` e `attemptsString`;
- `parameters`, incluindo `seed`/`seedString` como string exata `uint64`;
- `worker`, com indice e total de shards;
- `gradientCalibration`, com algoritmo, loss, vetor de erro, gradiente,
  learning rates, exploracao e parametros calibrados;
- `simulatorResult` com ordem extraida, dezenas ordenadas, completude e tempo
  final;
- `artifactOutputDir` para CSV/result.txt quando houver match;
- `simulator.gitCommit`, `updatedAt` e `matchedAt`.

Observacao matematica: esta calibracao e uma busca retrospectiva por parametros
e seed que reproduzam um sorteio ja conhecido. Ela nao demonstra capacidade de
prever concursos futuros; e um processo de ajuste/overfitting do simulador aos
resultados historicos.

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
- Agente C++ de calibracao historica para MongoDB, com checkpoints e retomada
  por concurso.

## Nota para Monte Carlo

O binario executa uma simulacao por vez. Para milhares de rodadas, use seeds
diferentes e direcione cada saida para um diretorio distinto. O CSV e amostrado
por `--csv-interval`, separado do timestep interno, para reduzir volume de dados.
