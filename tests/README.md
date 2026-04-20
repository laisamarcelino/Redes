## Estrutura de Testes

Os testes ficam organizados por tipo e por modulo:

```text
tests/
├── unit/
│   └── <modulo>/
│       └── test_<nome>.c
├── integration/
│   └── <modulo>/
│       └── test_<nome>.c
└── build/
```

Convencoes:

- `tests/unit/`: testes isolados, com mocks, stubs ou inclusao direta do codigo sob teste.
- `tests/integration/`: testes contra implementacoes reais e dependentes do ambiente.
- `tests/build/`: binarios gerados pelo `make`.

Comandos principais:

- `make list-tests`
- `make build-tests`
- `make test-unit`
- `make test-integration`
- `make test`
- `make clean`

Para adicionar um novo teste:

1. Crie um arquivo `.c` em `tests/unit/<modulo>/` ou `tests/integration/<modulo>/`.
2. O `Makefile` descobrirá o teste automaticamente.
3. Se o teste de integracao precisar linkar fontes reais de `src/`, adicione uma entrada `EXTRA_SRCS_...` no `Makefile`.

Exemplo:

```make
EXTRA_SRCS_integration_protocol_test_crc := src/protocol.c src/common.c
```

Nesse exemplo, a chave foi derivada de `tests/integration/protocol/test_crc.c`.
