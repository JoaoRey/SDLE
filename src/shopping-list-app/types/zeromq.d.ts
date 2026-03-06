declare module 'zeromq' {
  export class Dealer {
    connect(endpoint: string): void;
    send(frames: any[]): Promise<void>;
    [Symbol.asyncIterator](): AsyncIterator<any>;
  }
}
