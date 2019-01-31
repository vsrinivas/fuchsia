# Cloud dashboard

Web dashboard allowing a signed-in user to inspect their Ledger state and
trigger cloud erase if needed.

This project is built using Angular and was scaffolded using
[Angular CLI] version 6.0.8.

## Prerequisites

 - make sure you have [nodejs] installed
 - install [Angular CLI]: `npm install -g @angular/cli`
 - install [Firebase CLI]: `npm install -g firebase-tools`
 - run `npm install` in the `cloud_dashboard` directory

## Development workflow

 - run `ng serve` for a dev server. Navigate to `http://localhost:4200/`. The
   app will automatically reload if you change any of the source files
 - run `ng test` to execute the unit tests via [Karma]

To get more help on the Angular CLI use `ng help` or go check out the [Angular
CLI README]

## Deployment

Run `firebase login` at least once. Then:

 - run `ng build --prod` to build the project. The build artifacts will be stored
   in the `dist/` directory
 - run `firebase deploy`

[Angular CLI]: https://github.com/angular/angular-cli
[nodejs]: https://nodejs.org
[Karma]: https://karma-runner.github.io
[Angular CLI README]: https://github.com/angular/angular-cli/blob/master/README.md
[Firebase CLI]: https://firebase.google.com/docs/cli/
