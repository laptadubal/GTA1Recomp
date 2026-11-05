# Как отправить изменения

Локально всё закоммичено в ветку `main`. Пуш не удался: у токена GitHub
(`x-access-token` из keychain) нет права Contents: write на
`laptadubal/GTA1Recomp`, а GitLab-репозиторий/ключ не настроены.

## Вариант 1: появился доступ к GitHub
```
git push origin main
```

## Вариант 2: GitLab
```
git remote add gitlab git@gitlab.com:<user>/<repo>.git
git push gitlab main
```

## Вариант 3: перенести коммиты вручную
В корне лежит `GTA1Recomp-all.bundle` (все ветки и история) и `patches/*.patch`
(17 патчей поверх origin/main).

Импорт bundle:
```
git clone GTA1Recomp-all.bundle GTA1Recomp-restored
```

Применить патчи к существующему клону:
```
git am patches/*.patch
```
