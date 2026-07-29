# bash completion for crom
_crom() {
    local cur prev opts
    cur=${COMP_WORDS[COMP_CWORD]}
    prev=${COMP_WORDS[COMP_CWORD-1]}

    case $prev in
        -t|--type)   COMPREPLY=($(compgen -W "f d l" -- "$cur")); return ;;
        --color)     COMPREPLY=($(compgen -W "auto always never" -- "$cur")); return ;;
        -n|--name|-g|--glob|-c|--content|-E|--exclude|-s|--size|-e|--exec|\
        -j|--threads|--depth|--max-results)
            return ;;
    esac

    opts="-n --name -g --glob -i --ignore-case --case-sensitive -E --exclude
          -c --content -t --type -s --size -H --hidden -u --unrestricted
          -L --follow --depth --max-results -e --exec -j --threads -a --text
          -0 --null --json --bar -q --no-bar --no-ignore --no-messages
          --no-config --color -h --help -V --version"

    if [[ $cur == -* ]]; then
        COMPREPLY=($(compgen -W "$opts" -- "$cur"))
    else
        COMPREPLY=($(compgen -d -- "$cur"))
    fi
}
complete -F _crom crom
