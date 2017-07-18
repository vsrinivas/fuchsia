// The file contents for the current environment will overwrite these during build.
// The build system defaults to the dev environment which uses `environment.ts`, but if you do
// `ng build --env=prod` then `environment.prod.ts` will be used instead.
// The list of which env maps to which file can be found in `.angular-cli.json`.

export const environment = {
  production: false,
  firebase: {
    apiKey: 'AIzaSyDzzuJILOn6riFPTXC36HlH6CEdliLapDA',
    authDomain: 'fuchsia-ledger.firebaseapp.com',
    databaseURL: 'https://fuchsia-ledger.firebaseio.com',
    projectId: 'fuchsia-ledger',
    storageBucket: 'fuchsia-ledger.appspot.com',
    messagingSenderId: '191622714118'
  }
};
