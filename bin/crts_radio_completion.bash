# TODO: To get bash completion for the filter modules that are installed
# we assume that the CRTS filter plugins are in a directory that keeps a
# fixed relative path to this file.  In the future we could parse the
# users PATH and COMP_WORDS[0] and then figure out the path to the CRTS
# filter plugins from parsing those two strings.  That may be more work
# than this.

# We want this to look in the CRTS filter modules plugin directory
# on-the-fly so that the user does not have to do anything, but put the
# plugin (DSO library file) into the CRTS filter modules plugin directory,
# to install plugins; and they are seen by this script and we get bash
# filter module name completion automatically.


function _crts_radio_complete()
{
    #echo "got COMP_LINE=${COMP_LINE}  COMP_POINT=${COMP_POINT} COMP_CWORD=$COMP_CWORD"
    #set | less
    local cur_word prev_word
    cur_word="${COMP_WORDS[COMP_CWORD]}"
    prev_word="${COMP_WORDS[COMP_CWORD-1]}"

    # COMPREPLY is the array of possible completions

    if [[ ${cur_word} == --f* ]] ; then
        # Just fill in: --filter
        COMPREPLY=("--filter")
        return 0 # done
    fi
    if [[ ${cur_word} == -f ]] ; then
        COMPREPLY=("-f")
        return 0 # done
    fi

    if [[ ${prev_word} != -f ]] && [[ ${prev_word} != "--filter" ]] ; then
        # We currently only do the filter option
        return 0 # done
    fi


    local cwd="$PWD"

    # TODO: For now we require that this bash file be in the same
    # directory as crts_radio which is requires that filter plugins be in
    # ../share/crts/plugins/Filters/ relative to that directory.

    local mod_dir="$(dirname ${BASH_SOURCE[0]})" || return 0
    mod_dir="$mod_dir"/../share/crts/plugins/Filters/


    cd "$mod_dir" || return 0 # We failed, oh well
    mod_dir="$PWD"
    cd "$cwd" || return 1 # We screwed this bash shell.

    local i
    local mod
    local mods=()

    for i in $mod_dir/*.so ; do
        if [ ! -f "$i" ] ; then
            continue
        fi
        mod="$(basename ${i%%.so})"
        mods+=($mod)
        #echo "mod=${mod}"
    done

    #echo "mod_dir=$mod_dir"

    # COMPREPLY is the array of possible completions
    COMPREPLY=()
    for i in "${mods[@]}" ; do
        if [[ "$i" = "${cur_word}"* ]] ; then
            COMPREPLY+=($i)
        fi
    done

    return 0 # done
}

# Register the function _crts_radio_complete to provide bash completion for
# the program crts_radio
complete -F _crts_radio_complete crts_radio
