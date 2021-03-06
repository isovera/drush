<?php

/**
 * @file
 *   Functions used when Drush is starting up.
 *
 * This file is included and used early in the Drush
 * startup process, before any other Drush APIs are
 * available.
 */

/**
 * Get the current enviornment.
 */
function drush_env() {
  // Fetch the current environment.  To ensure that
  // $_ENV is correctly populated, make sure that
  // the value of 'variables-order' in your php.ini
  // contains "E" ("Environment").  See:
  // http://us.php.net/manual/en/ini.core.php#ini.variables-order
  $env = $_ENV;

  // If PHP is not configured correctly, $_ENV will be
  // empty.  Drush counts on the fact that environment
  // variables will always be available, though, so we
  // need to repair this situation.  We can always access
  // individual environmnet values via getenv(); however,
  // there is no PHP API that will tell us all of the
  // available values, so we will get the environment
  // variable values using 'printenv'.
  if (empty($env)) {
    exec('printenv', $env_items);
    foreach ($env_items as $item) {
      // Each $item is 'key=value' or just 'key'.
      // If $item has no value, then explode will return
      // a single array, [0 => 'key'].  We add a default
      // value of [1 => 'value'] to cover this case.  If
      // explode returns two items, the default value is ignored.
      list($key, $value) = explode('=', $item, 2) + array(1 => '');
      $env[$key] = $value;
    }
  }

  return $env;
}

/**
 * Checks the provided location and return the appropriate
 * Drush wrapper or Drush launcher script, if found.
 */
function find_wrapper_or_launcher($location) {
  // We look for a "Drush wrapper" script that might
  // be stored in the root of a site.  If there is
  // no wrapper script, then we look for the
  // drush.launcher script in vendor/bin.  We try just a
  // few of the most common locations; if the user relocates
  // their vendor directory anywhere else, then they must
  // use a wrapper script to locate it.  See the comment in
  // 'examples/drush' for details.
  //
  // We are somewhat "loose" about whether we are looking
  // for "drush" or "drush.launcher", because in old versions
  // of Drush, the "drush launcher" was named "drush".
  // Otherwise, there wouldn't be any point in looking for
  // "drush.launcher" at the root, or "drush" in a vendor directory.
  // We also allow users to rename their drush wrapper to
  // 'drush.wrapper' to avoid conflicting with a directory named
  // 'drush' at the site root.
  $drush_locations = array(
    "drush",
    "vendor/bin/drush",
    "../vendor/bin/drush",
    "sites/all/vendor/bin/drush",
    "sites/all/drush/drush/drush",
    "drush/drush/drush",
  );

  foreach ($drush_locations as $d) {
    foreach (array('.launcher', '.wrapper', '') as $suffix) {
      if (is_file("$location/$d$suffix")) {
        return "$location/$d$suffix";
      }
    }
  }
  return "";
}

/**
 * Linux version of escapeshellarg().
 *
 * This is intended to work the same way that escapeshellarg() does on
 * Linux.  If we need to escape a string that will be used remotely on
 * a Linux system, then we need our own implementation of escapeshellarg,
 * because the Windows version behaves differently.
 */
function _drush_escapeshellarg_linux($arg, $raw = FALSE) {
  // For single quotes existing in the string, we will "exit"
  // single-quote mode, add a \' and then "re-enter"
  // single-quote mode.  The result of this is that
  // 'quote' becomes '\''quote'\''
  $arg = preg_replace('/\'/', '\'\\\'\'', $arg);

  // Replace "\t", "\n", "\r", "\0", "\x0B" with a whitespace.
  // Note that this replacement makes Drush's escapeshellarg work differently
  // than the built-in escapeshellarg in PHP on Linux, as these characters
  // usually are NOT replaced. However, this was done deliberately to be more
  // conservative when running _drush_escapeshellarg_linux on Windows
  // (this can happen when generating a command to run on a remote Linux server.)
  $arg = str_replace(array("\t", "\n", "\r", "\0", "\x0B"), ' ', $arg);

  // Only wrap with quotes when needed.
  if(!$raw) {
    // Add surrounding quotes.
    $arg = "'" . $arg . "'";
  }

  return $arg;
}

/**
 * drush_startup is called once, by the Drush "finder"
 * script -- the "drush" script at the Drush root.
 * It finds the correct Drush "wrapper" or "launcher"
 * script to use, and executes it with process replacement.
 */
function drush_startup($argv) {
  $found_script = "";
  $home = getenv("HOME");
  $use_dir = "$home/.drush/use";

  // Get the arguments for the command.  Shift off argv[0],
  // which contains the name of this script.
  $arguments = $argv;
  array_shift($arguments);

  //
  // We need to do at least a partial parsing of the options,
  // so that we can find --root / -r and so on.
  //
  $VERBOSE=FALSE;
  $DEBUG=FALSE;
  $ROOT=FALSE;
  $COMMAND=FALSE;
  $ALIAS=FALSE;
  $VAR=FALSE;

  foreach ($arguments as $arg) {
    // If a variable to set was indicated on the
    // previous iteration, then set the value of
    // the named variable (e.g. "ROOT") to "$arg".
    if ($VAR) {
      $$VAR = "$arg";
      $VAR = FALSE;
    }
    else {
      switch ($arg) {
        case "-r":
          $VAR = "ROOT";
          break;

        case "--debug":
        case "-d":
          $DEBUG = TRUE;
          break;

        case "--verbose":
        case "-v":
          $VERBOSE = TRUE;
          break;
      }
      if (!$COMMAND && !$ALIAS && ($arg[0] == '@')) {
        $ALIAS = $arg;
      }
      elseif (!$COMMAND && ($arg[0] != '-')) {
        $COMMAND = $arg;
      }
      if (substr($arg, 0, 7) == "--root=") {
        $ROOT = substr($arg, 7);
      }
    }
  }

  $NONE=($ALIAS == "@none");

  //
  // If the @none alias is used, then we skip the Drush wrapper,
  // and call the Drush launcher directly.
  //
  // In this instance, we are assuming that the 'drush' that is being
  // called is:
  //
  //  a) The global 'drush', or
  //  b) A site-local 'drush' in a vendor/bin directory.
  //
  // In either event, the appropriate 'drush.launcher' should be right next
  // to this script (stored in the same directory).
  if ($NONE) {
    if (is_file(dirname(__DIR__) . "/drush.launcher")) {
      $found_script = dirname(__DIR__) . "/drush.launcher";
    }
    else {
      fwrite(STDERR, "Could not find drush.launcher in " . dirname(__DIR__) . ". Check your installation.\n");
      exit(1);
    }
  }

  //
  // Check for a root option:
  //
  //   drush --root=/path
  //
  // If the site root is specified via a commandline option, then we
  // should always use the Drush stored at this root, if there is one.
  // We will first check for a "wrapper" script at the root, and then
  // we will look for a "launcher" script in vendor/bin.
  //
  if (empty($found_script)) {
    $found_script = find_wrapper_or_launcher($ROOT);
    if (!empty($found_script)) {
      chdir($ROOT);
    }
  }

  //
  // If there is a .drush-use file, then its contents will
  // contain the path to the Drush to use.
  //
  if (empty($found_script)) {
    if (is_file(".drush-use")) {
      $found_script = file_get_contents(".drush-use");
    }
  }

  //
  // Look for a 'drush' wrapper or launcher at the cwd,
  // and in each of the directories above the cwd.  If
  // we find one, use it.
  //
  if (empty($found_script)) {
    $c = getcwd();
    while (!empty($c) && ($c != "/")) {
      $found_script = find_wrapper_or_launcher($c);
      if ($found_script) {
        chdir($c);
        break;
      }
      $c = dirname($c);
    }
  }

  if (!empty($found_script)) {
    $found_script = realpath($found_script);

    // Guard against errors:  if we have found a "drush" script
    // (that is, theoretically a drush wrapper script), and
    // there is a "drush.launcher" script in the same directory,
    // then we will skip the "drush" script and use the drush launcher
    // instead.  This is because drush "wrapper" scripts should
    // only ever exist at the root of a site, and there should
    // never be a drush "launcher" at the root of a site.
    // Therefore, if we find a "drush.launcher" next to a script
    // called "drush", we have probably found a Drush install directory,
    // not a site root.  Adjust appropriately.  Note that this
    // also catches the case where a drush "finder" script finds itself.
    if (is_file(dirname($found_script) . "/drush.launcher")) {
      $found_script = dirname($found_script) . "/drush.launcher";
    }
  }

  // Didn't find any site-local Drush, or @use'd Drush?
  // In that case, there should always be a drush.launcher in
  // the same directory this script is stored in; use that.
  if (empty($found_script)) {
    $found_script = dirname(__DIR__) . "/drush.launcher";
  }

  // Emit a message in verbose mode advertising the location of the
  // script we found.
  if ($VERBOSE) {
    fwrite(STDERR, "Using the Drush script found at $found_script\n");
  }

  if (function_exists("pcntl_exec")) {
    // Get the current environment for pnctl_exec.
    $env = drush_env();

    // Launch the new script in the same process.
    // If the launch succeeds, then it will not return.
    pcntl_exec($found_script, $arguments, $env);
    exit(1);
  }
  else {
    $escaped_args = array_map(function($item) { return _drush_escapeshellarg_linux($item); }, $arguments);
    passthru($found_script . ' ' . implode(' ', $escaped_args), $status_code);
    exit($status_code);
  }
}
